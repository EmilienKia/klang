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

#include "helpers.hpp"

using namespace k::model;
using namespace k::model::gen;
using namespace k::parse;
using namespace k::parse::ast;

TEST_CASE("Union lifecycle rejects an imported constructor with unknown exception contract",
          "[gen][union][copy][import][exceptions][regression]") {
    TmpKdi library(R"K(
        module unionlegacy;
        public struct Payload {
            Payload() {}
            Payload(other: const Payload&) {}
            ~Payload() {}
        }
    )K", true);
    auto make_compiler = [&]() {
        auto compiler = k::compiler::create();
        auto resolver = std::make_shared<k::path_lookup_file_resolver>();
        resolver->add_explicit_path("unionlegacy", library.kdi_path);
        resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
        compiler->set_file_resolver(resolver);
        return compiler;
    };
    const auto copy_union = R"K(
        module unionlegacy_consumer;
        import unionlegacy;
        union Storage { empty: byte; value: unionlegacy::Payload; }
        copy(value: const Storage&) { result: Storage = value; }
    )K";
    REQUIRE_NOTHROW(make_compiler()->parse_source("unionlegacy_known.k", copy_union, false, false));

    auto metadata = kdi::kdi_read_cbor_file(library.kdi_path);
    bool found_copy = false;
    for (auto& agg : metadata.unit.root_ns.aggregates) {
        if (agg.name != "Payload") continue;
        for (auto& ctor : agg.constructors) {
            if (!ctor.is_copy_constructor) continue;
            REQUIRE(ctor.throws_spec);
            ctor.throws_spec.reset();
            found_copy = true;
        }
    }
    REQUIRE(found_copy);
    REQUIRE(kdi::kdi_write_cbor_file(metadata, library.kdi_path));

    auto has_regeneration_diagnostic = Catch::Matchers::Predicate<k::log::compiler_error>(
        [](const auto& error) {
            return error.get_diagnostic().message.find("regenerate its KDI descriptor") != std::string::npos;
        }, "requires KDI descriptor regeneration");
    REQUIRE_THROWS_MATCHES(
        make_compiler()->parse_source("unionlegacy_unknown.k", copy_union, false, false),
        k::log::compiler_error, has_regeneration_diagnostic);
    REQUIRE_THROWS_MATCHES(make_compiler()->parse_source("unionlegacy_activation.k", R"K(
        module unionlegacy_activation;
        import unionlegacy;
        union Storage { empty: byte; value: unionlegacy::Payload; }
        make(value: const unionlegacy::Payload&) { result: Storage(value); }
    )K", false, false), k::log::compiler_error, has_regeneration_diagnostic);
    REQUIRE_NOTHROW(make_compiler()->parse_source("unionlegacy_ordinary.k", R"K(
        module unionlegacy_ordinary;
        import unionlegacy;
        copy(value: const unionlegacy::Payload&) { result: unionlegacy::Payload = value; }
    )K", false, false));
}

TEST_CASE("Union payload copies enforce checked constructor exceptions",
          "[gen][union][copy][exceptions][contract][regression]") {
    const std::string declarations = R"K(
        module union_checked_copy;
        class CopyError : public Exception {}
        struct Payload {
            Payload() {}
            Payload(other: const Payload&) throws(CopyError) {}
        }
        union Storage { empty: byte; value: Payload; }
    )K";
    auto compile = [&](const std::string& body) {
        auto compiler = k::compiler::create();
        auto resolver = std::make_shared<k::path_lookup_file_resolver>();
        resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
        compiler->set_file_resolver(resolver);
        compiler->parse_source("union_checked_copy.k", declarations + body, false, false);
    };
    auto uncaught = Catch::Matchers::Predicate<k::log::compiler_error>(
        [](const auto& error) {
            return error.get_diagnostic().message.find("neither caught nor declared") != std::string::npos;
        }, "rejects an unhandled copy constructor exception");
    REQUIRE_NOTHROW(compile(R"K(
        copy(value: const Storage&) throws(CopyError) { result: Storage = value; }
        activate(value: const Payload&) throws(CopyError) { result: Storage(value); }
    )K"));
    REQUIRE_THROWS_MATCHES(compile(R"K(
        copy(value: const Storage&) throws() { result: Storage = value; }
    )K"), k::log::compiler_error, uncaught);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        activate(value: const Payload&) throws() { result: Storage(value); }
    )K"), k::log::compiler_error, uncaught);
}

TEST_CASE("Union copies respect const alternatives and unconstructed first payloads",
          "[gen][union][copy][const][lifecycle][regression]") {
    auto jit = gen_jit(R"K(
        module union_const_alternative_copy;
        mutableCopies: int = 0;
        constCopies: int = 0;
        drops: int = 0;
        class Value {
        public:
            Value() {}
            Value(other: Value&) { ++mutableCopies; }
            Value(other: const Value&) { ++constCopies; }
            ~Value() { ++drops; }
        }
        union Frozen { const value: Value; number: int; }
        union Mutable { value: Value; number: int; }
        test(): int {
            mutableCopies = 0; constCopies = 0; drops = 0;
            {
                cold: Frozen;
                coldCopy: Frozen = cold;
                if (mutableCopies != 0 || constCopies != 0 || drops != 0) return -1;
                source: Value;
                frozen: Frozen(source);
                ordinary: Mutable(source);
                mutableCopies = 0; constCopies = 0;
                frozenCopy: Frozen = frozen;
                ordinaryCopy: Mutable = ordinary;
                if (mutableCopies != 1 || constCopies != 1 || drops != 0) return -2;
            }
            return drops == 5 ? 42 : -3;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Const union alternatives reject mutable-only ownership copies",
          "[gen][union][wrapper][nested][copy][const][owner][regression]") {
    auto jit = gen_jit(R"K(
        module union_const_alternative_owner;
        copies: int = 0;
        keyDrops: int = 0;
        resourceDrops: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() { ++resourceDrops; }
        }
        class Key {
            owned: Resource!;
        public:
            Key(): owned(new Resource()) {}
            Key(other: Key&) { ++copies; }
            Key(other: const Key&) -> delete;
            ~Key() { ++keyDrops; }
        }
        union Storage { const key: Key; number: int; }
        struct Inner { data: Storage; }
        struct Outer { inner: Inner; }
        test(): int {
            copies = 0; keyDrops = 0; resourceDrops = 0;
            {
                ordinary: Storage;
                ordinary.number = 42;
                copy: Storage = ordinary;
                if (copy.number != 42) return -1;
                active: Storage(Key());
                caught: bool = false;
                try { forbidden: Storage = active; }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || copies != 0 || keyDrops != 0 || resourceDrops != 0) return -2;
                wrapper: Outer;
                wrapper.inner.data = Storage(Key());
                caught = false;
                try { forbiddenWrapper: Outer = wrapper; }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || copies != 0 || keyDrops != 0 || resourceDrops != 0) return -3;
            }
            return keyDrops == 2 && resourceDrops == 2 ? 42 : -4;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Throwing const alternative copies leave the destination inactive",
          "[gen][union][copy][const][exceptions][lifecycle][regression]") {
    auto jit = gen_jit(R"K(
        module union_const_alternative_throw;
        fail: bool = false;
        mutableCopies: int = 0;
        constCopies: int = 0;
        drops: int = 0;
        class Value {
        public:
            Value() {}
            Value(other: Value&) { ++mutableCopies; }
            Value(other: const Value&) {
                if (fail) throw FatalError(925);
                ++constCopies;
            }
            ~Value() { ++drops; }
        }
        union Storage { const value: Value; number: int; }
        test(): int {
            fail = false; mutableCopies = 0; constCopies = 0; drops = 0;
            {
                source: Storage(Value());
                dest: Storage(Value());
                fail = true;
                code: int = 0;
                try { dest = source; }
                catch (error: FatalError&) { code = error.getCode(); }
                if (code != 925 || mutableCopies != 0 || constCopies != 0 || drops != 1)
                    return -1;
                if (dest.index() == Storage::Kind::value || dest.index() == Storage::Kind::number)
                    return -2;
                fail = false;
                dest = source;
                if (mutableCopies != 0 || constCopies != 1 || drops != 1) return -3;
            }
            return drops == 3 ? 42 : -4;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union copy contracts consider both mutable and const alternatives",
          "[gen][union][wrapper][nested][copy][const][contract][regression]") {
    const std::string declarations = R"K(
        module union_const_alternative_contract;
        class CopyError : public Exception {}
        struct Payload {
            Payload() {}
            Payload(other: Payload&) {}
            Payload(other: const Payload&) throws(CopyError) {}
        }
        union Storage { mutableValue: Payload; const frozenValue: Payload; }
        struct Inner { data: Storage; }
        struct Outer { inner: Inner; }
    )K";
    auto compile = [&](const std::string& body) {
        auto compiler = k::compiler::create();
        auto resolver = std::make_shared<k::path_lookup_file_resolver>();
        resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
        compiler->set_file_resolver(resolver);
        compiler->parse_source("union_const_contract.k", declarations + body, false, false);
    };
    auto uncaught = Catch::Matchers::Predicate<k::log::compiler_error>(
        [](const auto& error) {
            return error.get_diagnostic().message.find("neither caught nor declared") != std::string::npos;
        }, "rejects an unhandled const copy constructor exception");
    REQUIRE_NOTHROW(compile(R"K(
        copy(value: Storage&) throws(CopyError) { result: Storage = value; }
        copyWrapper(value: Outer&) throws(CopyError) { result: Outer = value; }
    )K"));
    REQUIRE_THROWS_MATCHES(compile(R"K(
        copy(value: Storage&) throws() { result: Storage = value; }
    )K"), k::log::compiler_error, uncaught);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        copy(value: Outer&) throws() { result: Outer = value; }
    )K"), k::log::compiler_error, uncaught);
}

TEST_CASE("Scalar union wrappers copy only the active payload",
          "[gen][union][wrapper][copy][regression]") {
    auto jit = gen_jit(R"K(
        module scalar_union_wrapper_copy;
        copies: int = 0;
        constCopies: int = 0;
        drops: int = 0;
        class Value {
        public:
            number: int = 42;
            Value() {}
            Value(other: Value&) : number(other.number) { ++copies; }
            Value(other: const Value&) : number(other.number) { ++constCopies; }
            ~Value() { ++drops; }
        }
        union Storage { empty: byte; value: Value; }
        struct Wrapper { marker: int; data: Storage; }
        consume(value: Wrapper): int { return value.data.value.number; }
        test(): int {
            copies = 0; constCopies = 0; drops = 0;
            {
                source: Value;
                first: Wrapper;
                first.marker = 7;
                first.data = Storage(source);
                second: Wrapper = first;
                second = second;
                third: Wrapper;
                third = first;
                readonly: const Wrapper& = first;
                fourth: Wrapper = readonly;
                if (second.marker != 7 || third.marker != 7 || consume(first) != 42) return -1;
                if (copies != 4 || constCopies != 1 || drops != 1) return -2;
            }
            return drops == 6 ? 42 : -3;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Scalar union wrapper assignment transfers a temporary owner",
          "[gen][union][wrapper][move][regression]") {
    auto jit = gen_jit(R"K(
        module scalar_union_wrapper_move;
        drops: int = 0;
        class Key {
        public:
            Key() {}
            ~Key() { ++drops; }
        }
        union Storage { number: int; key: Key!; }
        struct Wrapper {
            data: Storage;
            Wrapper(key: Key!) : data(key) {}
        }
        test(): int {
            drops = 0;
            {
                first: Wrapper(new Key());
                first = Wrapper(new Key());
                if (drops != 1) return -1;
            }
            return drops == 2 ? 42 : -2;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Scalar union wrappers preserve explicit copy operations",
          "[gen][union][wrapper][copy][regression]") {
    auto jit = gen_jit(R"K(
        module scalar_union_wrapper_explicit;
        union Storage { number: int; }
        struct Wrapper {
            data: Storage;
            marker: int;
            Wrapper() {}
            Wrapper(other: Wrapper&) : data(other.data), marker(99) {}
            operator =(other: const Wrapper&): Wrapper& {
                marker = other.marker + 100;
                return this;
            }
        }
        test(): int {
            first: Wrapper;
            first.data.number = 7;
            first.marker = 3;
            second: Wrapper = first;
            third: Wrapper;
            third.data.number = 1;
            third = first;
            return second.data.number == 7 && second.marker == 99
                && third.data.number == 1 && third.marker == 103 ? 42 : -1;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Scalar union wrapper rejects a noncopyable active payload",
          "[gen][union][wrapper][copy][regression]") {
    auto jit = gen_jit(R"K(
        module scalar_union_wrapper_noncopyable;
        drops: int = 0;
        class Key {
        private:
            Key(other: const Key&) -> delete;
        public:
            Key() {}
            ~Key() { ++drops; }
        }
        union Storage { number: int; key: Key; }
        struct Wrapper { data: Storage; marker: int; }
        test(): int {
            drops = 0;
            {
                ordinary: Wrapper;
                ordinary.data.number = 42;
                copied: Wrapper = ordinary;
                if (copied.data.number != 42) return -1;
                owned: Wrapper;
                owned.data = Storage(Key());
                caught: bool = false;
                try { forbidden: Wrapper = owned; }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || drops != 0) return -2;
                caught = false;
                try { copied = owned; }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || drops != 0) return -3;
                copied.data.number = 19;
            }
            return drops == 1 ? 42 : -4;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("A throwing scalar union wrapper copy destroys only completed objects",
          "[gen][union][wrapper][copy][exceptions][regression]") {
    auto jit = gen_jit(R"K(
        module scalar_union_wrapper_throw;
        fail: bool = false;
        copies: int = 0;
        drops: int = 0;
        class Value {
        public:
            Value() {}
            Value(other: const Value&) {
                if (fail) throw FatalError(927);
                ++copies;
            }
            ~Value() { ++drops; }
        }
        union Storage { empty: byte; value: Value; }
        struct Wrapper { marker: int; data: Storage; }
        test(): int {
            fail = false; copies = 0; drops = 0;
            {
                source: Value;
                first: Wrapper;
                first.data = Storage(source);
                replacement: Wrapper;
                replacement.data = Storage(source);
                fail = true;
                code: int = 0;
                try { second: Wrapper = first; }
                catch (error: FatalError&) { code = error.getCode(); }
                if (code != 927 || copies != 2 || drops != 0) return -1;
                code = 0;
                try { replacement = first; }
                catch (error: FatalError&) { code = error.getCode(); }
                if (code != 927 || copies != 2 || drops != 1) return -2;
                replacement.data.empty = 1;
                fail = false;
            }
            return drops == 3 ? 42 : -3;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Copying a default scalar union wrapper does not construct its first payload",
          "[gen][union][wrapper][copy][default][regression]") {
    auto jit = gen_jit(R"K(
        module scalar_union_wrapper_default;
        constructs: int = 0;
        drops: int = 0;
        struct Value {
            Value() { ++constructs; }
            Value(other: const Value&) { ++constructs; }
            ~Value() { ++drops; }
        }
        union Storage { value: Value; number: int; }
        struct Wrapper { data: Storage; marker: int; }
        test(): int {
            constructs = 0; drops = 0;
            {
                first: Wrapper;
                second: Wrapper = first;
                if (first.data.index() != 0 || second.data.index() != 0) return -1;
            }
            return constructs == 0 && drops == 0 ? 42 : -2;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Scalar union wrapper copies enforce payload exception contracts",
          "[gen][union][wrapper][copy][exceptions][contract][regression]") {
    const std::string declarations = R"K(
        module scalar_union_wrapper_checked;
        class CopyError : public Exception {}
        struct Payload {
            Payload() {}
            Payload(other: const Payload&) throws(CopyError) {}
        }
        union Storage { empty: byte; value: Payload; }
        struct Wrapper { marker: int; data: Storage; }
        consume(value: Wrapper) {}
    )K";
    auto compile = [&](const std::string& body) {
        auto compiler = k::compiler::create();
        auto resolver = std::make_shared<k::path_lookup_file_resolver>();
        resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
        compiler->set_file_resolver(resolver);
        compiler->parse_source("scalar_union_wrapper_checked.k", declarations + body, false, false);
    };
    auto uncaught = Catch::Matchers::Predicate<k::log::compiler_error>(
        [](const auto& error) {
            return error.get_diagnostic().message.find("neither caught nor declared") != std::string::npos;
        }, "rejects an unhandled payload copy exception");
    REQUIRE_NOTHROW(compile(R"K(
        copy(value: const Wrapper&) throws(CopyError) { result: Wrapper = value; }
        temporary(value: const Wrapper&) throws(CopyError) { result: Wrapper = Wrapper(value); }
        assign(dest: Wrapper&, value: const Wrapper&) throws(CopyError) { dest = value; }
        pass(value: const Wrapper&) throws(CopyError) { consume(value); }
        caught(value: const Wrapper&) throws() {
            try { result: Wrapper = value; } catch (error: CopyError&) {}
        }
    )K"));
    REQUIRE_THROWS_MATCHES(compile(R"K(
        copy(value: const Wrapper&) throws() { result: Wrapper = value; }
    )K"), k::log::compiler_error, uncaught);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        temporary(value: const Wrapper&) throws() { result: Wrapper = Wrapper(value); }
    )K"), k::log::compiler_error, uncaught);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        assign(dest: Wrapper&, value: const Wrapper&) throws() { dest = value; }
    )K"), k::log::compiler_error, uncaught);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        pass(value: const Wrapper&) throws() { consume(value); }
    )K"), k::log::compiler_error, uncaught);
}

TEST_CASE("Nested scalar union wrappers preserve active copy and destruction",
          "[gen][union][wrapper][nested][copy][regression]") {
    auto jit = gen_jit(R"K(
        module nested_scalar_union_wrapper_copy;
        copies: int = 0;
        constCopies: int = 0;
        drops: int = 0;
        class Value {
        public:
            number: int = 42;
            Value() {}
            Value(other: Value&) : number(other.number) { ++copies; }
            Value(other: const Value&) : number(other.number) { ++constCopies; }
            ~Value() { ++drops; }
        }
        union Storage { empty: byte; value: Value; }
        struct Inner { data: Storage; marker: int; }
        struct Middle { inner: Inner; marker: int; }
        struct Outer { middle: Middle; marker: int; }
        consume(value: Outer): int { return value.middle.inner.data.value.number; }
        test(): int {
            copies = 0; constCopies = 0; drops = 0;
            {
                source: Value;
                first: Outer;
                first.middle.inner.data = Storage(source);
                first.middle.inner.marker = 7;
                first.middle.marker = 8;
                first.marker = 9;
                second: Outer = first;
                second = second;
                third: Outer;
                third = first;
                readonly: const Outer& = first;
                fourth: Outer = readonly;
                if (second.middle.inner.marker != 7 || third.middle.marker != 8
                    || fourth.marker != 9 || consume(first) != 42) return -1;
                if (copies != 4 || constCopies != 1 || drops != 1) return -2;
            }
            return drops == 6 ? 42 : -3;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Nested scalar union wrappers reject noncopyable active payloads",
          "[gen][union][wrapper][nested][copy][regression]") {
    auto jit = gen_jit(R"K(
        module nested_scalar_union_wrapper_noncopyable;
        drops: int = 0;
        class Key {
        private:
            Key(other: const Key&) -> delete;
        public:
            Key() {}
            ~Key() { ++drops; }
        }
        union Storage { number: int; key: Key; }
        struct Inner { data: Storage; }
        struct Outer { inner: Inner; marker: int; }
        test(): int {
            drops = 0;
            {
                ordinary: Outer;
                ordinary.inner.data.number = 42;
                copied: Outer = ordinary;
                if (copied.inner.data.number != 42) return -1;
                owned: Outer;
                owned.inner.data = Storage(Key());
                caught: bool = false;
                try { forbidden: Outer = owned; }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || drops != 0) return -2;
                caught = false;
                try { copied = owned; }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || drops != 0) return -3;
                copied.inner.data.number = 19;
            }
            return drops == 1 ? 42 : -4;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Nested union wrappers preserve explicit inner copy operations",
          "[gen][union][wrapper][nested][copy][regression]") {
    auto jit = gen_jit(R"K(
        module nested_scalar_union_wrapper_explicit;
        union Storage { number: int; }
        struct Inner {
            data: Storage;
            marker: int;
            Inner() {}
            Inner(other: Inner&) : data(other.data), marker(99) {}
            operator =(other: const Inner&): Inner& {
                marker = other.marker + 100;
                return this;
            }
        }
        struct Outer {
            inner: Inner;
            Outer() {}
            Outer(other: Outer&) : inner(other.inner) {}
            operator =(other: const Outer&): Outer& {
                inner = other.inner;
                return this;
            }
        }
        test(): int {
            first: Outer;
            first.inner.data.number = 7;
            first.inner.marker = 3;
            second: Outer = first;
            third: Outer;
            third.inner.data.number = 1;
            third = first;
            return second.inner.data.number == 7 && second.inner.marker == 99
                && third.inner.data.number == 1 && third.inner.marker == 103 ? 42 : -1;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Nested scalar union wrappers transfer temporary owners",
          "[gen][union][wrapper][nested][move][regression]") {
    auto jit = gen_jit(R"K(
        module nested_scalar_union_wrapper_move;
        drops: int = 0;
        class Key {
        public:
            Key() {}
            ~Key() { ++drops; }
        }
        union Storage { number: int; key: Key!; }
        struct Inner {
            data: Storage;
            Inner(key: Key!) : data(key) {}
        }
        struct Outer {
            inner: Inner;
            Outer(key: Key!) : inner(key) {}
        }
        test(): int {
            drops = 0;
            {
                first: Outer(new Key());
                first = Outer(new Key());
                if (drops != 1) return -1;
            }
            return drops == 2 ? 42 : -2;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Nested scalar union wrapper move cleans its temporary when replacement throws",
          "[gen][union][wrapper][nested][move][exceptions][regression]") {
    auto jit = gen_jit(R"K(
        module nested_scalar_union_wrapper_move_throw;
        oldDrops: int = 0;
        keyDrops: int = 0;
        class Old {
        public:
            Old() {}
            ~Old() { ++oldDrops; throw FatalError(929); }
        }
        class Key {
        public:
            Key() {}
            ~Key() { ++keyDrops; }
        }
        union Storage { empty: byte; old: Old; key: Key!; }
        struct Inner {
            data: Storage;
            Inner() {}
            Inner(key: Key!) : data(key) {}
        }
        struct Outer {
            inner: Inner;
            Outer() {}
            Outer(key: Key!) : inner(key) {}
        }
        test(): int {
            oldDrops = 0; keyDrops = 0;
            {
                target: Outer;
                target.inner.data = Storage(Old());
                caught: bool = false;
                try { target = Outer(new Key()); }
                catch (error: FatalError&) { caught = error.getCode() == 929; }
                if (!caught || oldDrops != 1 || keyDrops != 1) return -1;
            }
            return oldDrops == 1 && keyDrops == 1 ? 42 : -2;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Nested scalar union wrapper copy unwinds without duplicate destruction",
          "[gen][union][wrapper][nested][copy][exceptions][regression]") {
    auto jit = gen_jit(R"K(
        module nested_scalar_union_wrapper_throw;
        fail: bool = false;
        copies: int = 0;
        drops: int = 0;
        class Value {
        public:
            Value() {}
            Value(other: const Value&) {
                if (fail) throw FatalError(928);
                ++copies;
            }
            ~Value() { ++drops; }
        }
        union Storage { empty: byte; value: Value; }
        struct Inner { data: Storage; marker: int; }
        struct Outer { marker: int; inner: Inner; }
        test(): int {
            fail = false; copies = 0; drops = 0;
            {
                source: Value;
                first: Outer;
                first.inner.data = Storage(source);
                replacement: Outer;
                replacement.inner.data = Storage(source);
                fail = true;
                code: int = 0;
                try { second: Outer = first; }
                catch (error: FatalError&) { code = error.getCode(); }
                if (code != 928 || copies != 2 || drops != 0) return -1;
                code = 0;
                try { replacement = first; }
                catch (error: FatalError&) { code = error.getCode(); }
                if (code != 928 || copies != 2 || drops != 1) return -2;
                replacement.inner.data.empty = 1;
                fail = false;
            }
            return drops == 3 ? 42 : -3;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Nested scalar union wrapper retains the default unconstructed alternative",
          "[gen][union][wrapper][nested][copy][default][regression]") {
    auto jit = gen_jit(R"K(
        module nested_scalar_union_wrapper_default;
        constructs: int = 0;
        drops: int = 0;
        struct Value {
            Value() { ++constructs; }
            Value(other: const Value&) { ++constructs; }
            ~Value() { ++drops; }
        }
        union Storage { value: Value; number: int; }
        struct Inner { data: Storage; }
        struct Outer { inner: Inner; }
        test(): int {
            constructs = 0; drops = 0;
            {
                first: Outer;
                second: Outer = first;
                if (first.inner.data.index() != 0 || second.inner.data.index() != 0) return -1;
            }
            return constructs == 0 && drops == 0 ? 42 : -2;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Nested scalar union wrapper copies enforce payload exception contracts",
          "[gen][union][wrapper][nested][copy][exceptions][contract][regression]") {
    const std::string declarations = R"K(
        module nested_scalar_union_wrapper_checked;
        class CopyError : public Exception {}
        struct Payload {
            Payload() {}
            Payload(other: const Payload&) throws(CopyError) {}
        }
        union Storage { empty: byte; value: Payload; }
        struct Inner { data: Storage; }
        struct Outer { inner: Inner; marker: int; }
        consume(value: Outer) {}
    )K";
    auto compile = [&](const std::string& body) {
        auto compiler = k::compiler::create();
        auto resolver = std::make_shared<k::path_lookup_file_resolver>();
        resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
        compiler->set_file_resolver(resolver);
        compiler->parse_source("nested_scalar_union_wrapper_checked.k", declarations + body, false, false);
    };
    auto uncaught = Catch::Matchers::Predicate<k::log::compiler_error>(
        [](const auto& error) {
            return error.get_diagnostic().message.find("neither caught nor declared") != std::string::npos;
        }, "rejects an unhandled payload copy exception");
    REQUIRE_NOTHROW(compile(R"K(
        copy(value: const Outer&) throws(CopyError) { result: Outer = value; }
        temporary(value: const Outer&) throws(CopyError) { result: Outer = Outer(value); }
        assign(dest: Outer&, value: const Outer&) throws(CopyError) { dest = value; }
        pass(value: const Outer&) throws(CopyError) { consume(value); }
        caught(value: const Outer&) throws() {
            try { result: Outer = value; } catch (error: CopyError&) {}
        }
    )K"));
    REQUIRE_THROWS_MATCHES(compile(R"K(
        copy(value: const Outer&) throws() { result: Outer = value; }
    )K"), k::log::compiler_error, uncaught);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        temporary(value: const Outer&) throws() { result: Outer = Outer(value); }
    )K"), k::log::compiler_error, uncaught);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        assign(dest: Outer&, value: const Outer&) throws() { dest = value; }
    )K"), k::log::compiler_error, uncaught);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        pass(value: const Outer&) throws() { consume(value); }
    )K"), k::log::compiler_error, uncaught);
}

TEST_CASE("Union owner alternatives transfer their source on construction",
          "[gen][union][lifecycle][owner][regression]") {
    auto jit = gen_jit(R"K(
        module union_owner_construct;
        drops: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() { ++drops; }
        }
        union Storage { empty: byte; value: Resource!; }
        test(): int {
            drops = 0;
            {
                first: Resource! = new Resource();
                second: Resource! = new Resource();
                a: Storage(first);
                b: Storage = Storage(second);
                if (first != null || second != null || drops != 0) return -1;
            }
            return drops == 2 ? 42 : -2;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("A union temporary constructs a distinct union alternative",
          "[gen][union][construction][copy][regression]") {
    auto jit = gen_jit(R"K(
        module union_nested_value_construction;
        union Inner { number: int; }
        union Outer { empty: byte; value: Inner; }
        test(): int {
            original: Inner(42);
            outer: Outer = Outer(original);
            copied: Inner = outer.value;
            return outer.index() == 1 && copied.number == 42 ? 42 : -1;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union local is not destroyed when unwinding before its declaration",
          "[gen][union][lifecycle][exceptions][regression]") {
    auto jit = gen_jit(R"K(
        module union_unreached_local;
        drops: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() { ++drops; }
        }
        union Storage { empty: byte; value: Resource; }
        fail() { throw FatalError(717); }
        test(): int {
            drops = 0;
            code: int = 0;
            try {
                fail();
                local: Storage(Resource());
            } catch (error: FatalError&) { code = error.getCode(); }
            return code == 717 && drops == 0 ? 42 : -1;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union replacement unwinds incoming owners and temporaries",
          "[gen][union][lifecycle][exceptions][owner][regression]") {
    auto jit = gen_jit(R"K(
        module union_replacement_unwind;
        oldDrops: int = 0;
        newDrops: int = 0;
        class Old {
        public:
            Old() {}
            ~Old() { ++oldDrops; throw FatalError(718); }
        }
        class Incoming {
        public:
            Incoming() {}
            ~Incoming() { ++newDrops; }
        }
        union Storage { old: Old; owned: Incoming!; value: Incoming; }
        test(): int {
            oldDrops = 0; newDrops = 0;
            {
                first: Storage(Old());
                incoming: Incoming! = new Incoming();
                caught: bool = false;
                try { first.owned = incoming; }
                catch (error: FatalError&) { caught = error.getCode() == 718; }
                if (!caught || incoming != null || oldDrops != 1 || newDrops != 1) return -1;
                second: Storage(Old());
                caught = false;
                try { second.value = Incoming(); }
                catch (error: FatalError&) { caught = error.getCode() == 718; }
                if (!caught || oldDrops != 2 || newDrops != 2) return -2;
            }
            return oldDrops == 2 && newDrops == 2 ? 42 : -3;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union array cleanup continues after an element destructor throws",
          "[gen][union][lifecycle][array][exceptions][regression]") {
    auto jit = gen_jit(R"K(
        module union_array_unwind;
        nextId: int = 0;
        copies: int = 0;
        order: int = 0;
        fail: bool = false;
        class Item {
        public:
            id: int;
            Item() : id(nextId) { ++nextId; }
            Item(other: const Item&) : id(other.id) { ++copies; }
            ~Item() {
                order = order * 10 + id;
                if (fail && id == 1) throw FatalError(719);
            }
        }
        union Storage { empty: byte; items: Item[3]; }
        test(): int {
            nextId = 0; copies = 0; order = 0; fail = false;
            {
                source: Item[3];
                value: Storage(source);
                index: int = value.index();
                if (nextId != 3 || index != 1 || copies != 3) return -2;
                caught: bool = false;
                fail = true;
                try { value.empty = 1; }
                catch (error: FatalError&) { caught = error.getCode() == 719; }
                fail = false;
                if (!caught) return -3;
                if (order != 210) return -4;
            }
            return 42;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union copy declares its failure constructor without an explicit error reference",
          "[gen][union][copy][exceptions][regression]") {
    auto jit = gen_jit(R"K(
        module union_copy_implicit_error;
        class Key {
        public:
            Key() {}
            Key(other: const Key&) -> delete;
            ~Key() {}
        }
        union Storage { number: int; key: Key; }
        test(): int {
            first: Storage;
            first.number = 42;
            second: Storage = first;
            return second.number;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Whole union copies dispatch the active copy constructor and handle self assignment",
          "[gen][union][copy][lifecycle][regression]") {
    auto jit = gen_jit(R"K(
        module union_whole_copy;
        copies: int = 0;
        constCopies: int = 0;
        drops: int = 0;
        class Value {
        public:
            number: int = 42;
            Value() {}
            Value(other: Value&) : number(other.number) { ++copies; }
            Value(other: const Value&) : number(other.number) { ++constCopies; }
            ~Value() { ++drops; }
        }
        union Storage { empty: byte; value: Value; }
        consume(value: Storage): int { return value.value.number; }
        test(): int {
            copies = 0; constCopies = 0; drops = 0;
            {
                source: Value;
                first: Storage(source);
                second: Storage = first;
                second = second;
                readonly: const Storage& = first;
                third: Storage = readonly;
                fourth: Storage;
                fourth = first;
                if (consume(first) != 42) return -1;
                if (copies != 4 || constCopies != 1 || drops != 1) return -2;
            }
            return drops == 6 ? 42 : -3;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Whole union rejects a noncopyable active payload with an unwinding error",
          "[gen][union][copy][exceptions][regression]") {
    auto jit = gen_jit(R"K(
        module union_whole_noncopyable;
        drops: int = 0;
        markerDrops: int = 0;
        class Key {
        private:
            handle: int* = null;
            Key(other: const Key&) -> delete;
        public:
            Key() {}
            ~Key() { ++drops; }
        }
        class Marker {
        public:
            Marker() {}
            ~Marker() { ++markerDrops; }
        }
        union Storage { number: int; key: Key; }
        consume(marker: Marker, value: Storage) {}
        test(): int {
            drops = 0; markerDrops = 0;
            {
                first: Storage;
                first.number = 42;
                second: Storage = first;
                if (second.number != 42) return -1;
                owned: Storage(Key());
                caught: bool = false;
                try { forbidden: Storage = owned; }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || drops != 0) return -2;
                caught = false;
                try { consume(Marker(), owned); }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || drops != 0 || markerDrops != 1) return -4;
                caught = false;
                try { second = owned; }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || drops != 0) return -5;
                second.number = 19;
                if (second.number != 19) return -6;
            }
            return drops == 1 ? 42 : -3;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Copying a default union does not construct or destroy its first payload",
          "[gen][union][copy][default][lifecycle][regression]") {
    auto jit = gen_jit(R"K(
        module union_copy_default;
        constructs: int = 0;
        drops: int = 0;
        struct Value {
            Value() { ++constructs; }
            Value(other: const Value&) { ++constructs; }
            ~Value() { ++drops; }
        }
        union Storage { value: Value; number: int; }
        test(): int {
            constructs = 0; drops = 0;
            {
                first: Storage;
                second: Storage = first;
                if (first.index() != 0 || second.index() != 0) return -1;
            }
            return constructs == 0 && drops == 0 ? 42 : -2;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Throwing whole union copies clean up only completed payloads",
          "[gen][union][copy][exceptions][lifecycle][regression]") {
    auto jit = gen_jit(R"K(
        module union_copy_throw;
        fail: bool = false;
        copies: int = 0;
        drops: int = 0;
        class Value {
        public:
            Value() {}
            Value(other: const Value&) {
                if (fail) throw FatalError(921);
                ++copies;
            }
            ~Value() { ++drops; }
        }
        union Storage { empty: byte; value: Value; }
        test(): int {
            fail = false; copies = 0; drops = 0;
            {
                source: Value;
                first: Storage(source);
                fail = true;
                code: int = 0;
                try { second: Storage = first; }
                catch (error: FatalError&) { code = error.getCode(); }
                if (code != 921 || copies != 1 || drops != 0) return -1;
                fail = false;
            }
            return drops == 2 ? 42 : -2;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Inherited union assignment copies the active base payload",
          "[gen][union][copy][inheritance][regression]") {
    auto jit = gen_jit(R"K(
        module union_inherited_copy;
        copies: int = 0;
        drops: int = 0;
        class Value {
        public:
            number: int = 42;
            Value() {}
            Value(other: const Value&) : number(other.number) { ++copies; }
            ~Value() { ++drops; }
        }
        union Base { empty: byte; value: Value; }
        union Derived : Base { extra: int; }
        test(): int {
            copies = 0; drops = 0;
            {
                source: Value;
                derived: Derived;
                derived.value = source;
                base: Base;
                base = derived;
                another: Derived;
                another = base;
                if (copies != 3 || base.value.number != 42 || another.value.number != 42) return -1;
            }
            return drops == 4 ? 42 : -2;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union construction and activation select the matching copy overload",
          "[gen][union][copy][overload][regression]") {
    auto jit = gen_jit(R"K(
        module union_copy_overload;
        copies: int = 0;
        assignments: int = 0;
        class Value {
        public:
            value: int = 42;
            Value() {}
            Value(other: const Value&) -> delete;
            Value(other: Value&) : value(other.value) { ++copies; }
            operator=(other: const Value&): Value& {
                value = other.value;
                ++assignments;
                return this;
            }
        }
        union Storage { empty: byte; value: Value; }
        test(): int {
            source: Value;
            {
                constructed: Storage(source);
                storage: Storage;
                storage.value = source;
                storage.value = source;
                if (copies != 2 || assignments != 1) return -1;
            }
            return 42;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union construction transfers a noncopyable temporary",
          "[gen][union][copy][move][regression]") {
    auto jit = gen_jit(R"K(
        module union_move_temporary;
        drops: int = 0;
        class Value {
        public:
            Value() {}
            Value(other: const Value&) -> delete;
            ~Value() { ++drops; }
        }
        union Storage { empty: byte; value: Value; }
        test(): int {
            drops = 0;
            {
                storage: Storage(Value());
                if (drops != 0) return -1;
            }
            return drops;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Union activation rejects its selected deleted copy overload",
          "[gen][union][copy][overload][regression]") {
    REQUIRE_THROWS(gen_jit_throws(R"K(
        module union_deleted_activation;
        class Value {
        public:
            Value() {}
            Value(other: const Value&) {}
            Value(other: Value&) -> delete;
            operator=(other: const Value&): Value& { return this; }
        }
        union Storage { empty: byte; value: Value; }
        test(): int { source: Value; storage: Storage; storage.value = source; return 0; }
    )K"));
}

TEST_CASE("Aggregate union members destroy only their active alternative",
          "[gen][union][lifecycle][regression]") {
    auto jit = gen_jit(R"(
        module gen_union_member_lifetime;
        destroyed : int = 0;
        struct Tracked {
            Tracked() {}
            Tracked(other : Tracked&) {}
            ~Tracked() { ++destroyed; }
        }
        template<typename T>
        struct Holder {
            union Storage {
                value : T;
                error : int;
            }
            storage : Storage;
            Holder(value : T&) {
                storage.value = value;
            }
        }
        test() : int {
            destroyed = 0;
            source : Tracked;
            before : int = 0;
            {
                holder : Holder<Tracked>(source);
                before = destroyed;
            }
            if (destroyed != before + 1) return 1;
            {
                holder : Holder<Tracked>(source);
                holder.storage.error = 7;
                before = destroyed;
            }
            if (destroyed != before) return 2;
            return 0;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Union: class activation uses construction and failed switches leave no live payload",
          "[gen][union][class][lifecycle][exceptions][regression]") {
    auto jit = gen_jit(R"SRC(
        module union_class_activation;
        copies : int = 0;
        assignments : int = 0;
        drops : int = 0;
        invalid : int = 0;
        failCopy : bool = false;
        class Probe {
            magic : int = 73;
            Probe() {}
            Probe(other : const Probe&) {
                if (failCopy) throw Exception(922);
                if (other.magic != 73) ++invalid;
                ++copies;
            }
            operator=(other : const Probe&) : Probe& {
                if (magic != 73 || other.magic != 73) ++invalid;
                ++assignments;
                return this;
            }
            ~Probe() {
                if (magic != 73) ++invalid;
                magic = 0;
                ++drops;
            }
        }
        union Storage {
            empty : byte;
            result : Probe;
            error : Probe;
        }
        test() : int {
            copies = 0; assignments = 0; drops = 0; invalid = 0; failCopy = false;
            valid : bool = true;
            {
                source : Probe;
                {
                    storage : Storage;
                    storage.result = source;
                    valid = valid && copies == 1 && assignments == 0 && drops == 0;
                    storage.result = source;
                    valid = valid && copies == 1 && assignments == 1 && drops == 0;
                    failCopy = true;
                    code : int = 0;
                    try { storage.error = source; }
                    catch (e : Throwable&) { code = e.getCode(); }
                    valid = valid && code == 922 && drops == 1 && assignments == 1;
                    valid = valid && storage.index() != Storage::Kind::result;
                    valid = valid && storage.index() != Storage::Kind::error;
                    failCopy = false;
                    storage.result = source;
                    valid = valid && copies == 2 && drops == 1;
                    storage.empty = 0;
                    valid = valid && drops == 2 && storage.index() == Storage::Kind::empty;
                }
                valid = valid && drops == 2;
            }
            return valid && drops == 3 && invalid == 0 ? 1 : 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    int result = 0;
    REQUIRE_NOTHROW(result = fn());
    CHECK(result == 1);
}

// ============================================================
// Phase 1: Basic union declaration — parsing and type creation
// ============================================================

TEST_CASE("Union basic declaration compiles", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_01;
        union MyUnion {
            first: int;
            second: long;
        }
        get_size() : int {
            return 1;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("get_size");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Union variable declaration and default construction", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_02;
        union MyUnion {
            first: int;
            second: long;
        }
        test_default() : int {
            u : MyUnion;
            return 0;
        }
    )", false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_default");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

TEST_CASE("Union explicit member write and read", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_03;
        union MyUnion {
            first: int;
            second: long;
        }
        test_write_read() : int {
            u : MyUnion;
            u.first = 42;
            return u.first;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_write_read");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Union typed construction with initializer", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_04;
        union MyUnion {
            first: int;
            second: long;
        }
        test_init() : int {
            u : MyUnion = 25;
            return u.first;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_init");
    REQUIRE(fn);
    REQUIRE(fn() == 25);
}

TEST_CASE("Union second alternative access", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_05;
        union MyUnion {
            first: int;
            second: long;
        }
        test_second() : long {
            u : MyUnion;
            u.second = 100;
            return u.second;
        }
    )", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<long(*)()>("test_second");
    REQUIRE(fn);
    REQUIRE(fn() == 100L);
}

TEST_CASE("Union drain addresser rejected", "[gen][union]") {
    REQUIRE(compile_should_fail(R"(
        module gen_union_06;
        union MyUnion {
            first: int;
            second: int#;
        }
        dummy() : int { return 0; }
    )", nullptr));
}

TEST_CASE("Union multiple alternatives different sizes", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_07;
        union NumUnion {
            i: int;
            l: long;
            b: byte;
        }
        test_int() : int {
            u : NumUnion;
            u.i = 42;
            return u.i;
        }
        test_long() : long {
            u : NumUnion;
            u.l = 1000000;
            return u.l;
        }
        test_byte() : byte {
            u : NumUnion;
            u.b = 7;
            return u.b;
        }
    )");
    REQUIRE(jit);
    auto fn_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(fn_int);
    REQUIRE(fn_int() == 42);
    auto fn_long = jit->lookup_symbol<long(*)()>("test_long");
    REQUIRE(fn_long);
    REQUIRE(fn_long() == 1000000L);
    auto fn_byte = jit->lookup_symbol<int8_t(*)()>("test_byte");
    REQUIRE(fn_byte);
    REQUIRE(fn_byte() == 7);
}

TEST_CASE("Union passed to function by reference", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_08;
        union MyUnion {
            first: int;
            second: long;
        }
        read_first(u: MyUnion&) : int {
            return u.first;
        }
        test_ref() : int {
            u : MyUnion;
            u.first = 99;
            return read_first(u);
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_ref");
    REQUIRE(fn);
    REQUIRE(fn() == 99);
}

// ============================================================
// Phase 2: Union with struct alternatives
// ============================================================

TEST_CASE("Union with struct alternative — write and read fields", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module gen_union_09;
        struct Point {
            x: int = 0;
            y: int = 0;
        }
        union ShapeData {
            point: Point;
            radius: int;
        }
        test_struct_alt() : int {
            u : ShapeData;
            p : Point;
            p.x = 10;
            p.y = 20;
            u.point = p;
            return u.point.x + u.point.y;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_struct_alt");
    REQUIRE(fn);
    REQUIRE(fn() == 30);
}

TEST_CASE("Union with struct alternative — default construction", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module gen_union_10;
        struct Pair {
            a: int = 5;
            b: int = 7;
        }
        union PairOrInt {
            pair: Pair;
            value: int;
        }
        test_default_struct() : int {
            u : PairOrInt;
            return u.pair.a + u.pair.b;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_default_struct");
    REQUIRE(fn);
    // Default-initialized union has discriminant=0, first alt (Pair) is zero-initialized memory
    // struct fields are zero because storage is zero-inited, not default-constructed
    REQUIRE(fn() == 0);
}

TEST_CASE("Union with struct — switch between struct and primitive", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module gen_union_11;
        struct Vec2 {
            x: int = 0;
            y: int = 0;
        }
        union VecOrScalar {
            vec: Vec2;
            scalar: int;
        }
        test_switch() : int {
            u : VecOrScalar;
            u.scalar = 42;
            return u.scalar;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_switch");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Union with struct — struct field modification via reference", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module gen_union_12;
        struct Rect {
            w: int = 0;
            h: int = 0;
        }
        union Shape {
            rect: Rect;
            radius: int;
        }
        test_struct_ref() : int {
            u : Shape;
            r : Rect;
            r.w = 3;
            r.h = 4;
            u.rect = r;
            return u.rect.w * u.rect.h;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_struct_ref");
    REQUIRE(fn);
    REQUIRE(fn() == 12);
}



// ============================================================
// Phase 3: Union with class alternatives
// ============================================================

TEST_CASE("Union with class alternative — basic instantiation", "[gen][union][class]") {
    auto jit = gen_jit(R"(
        module gen_union_13;
        class Counter {
            count: int;
            Counter() : count(0) {}
            increment() { ++count; }
            get() : int { return count; }
        }
        union ValueOrCounter {
            value: int;
            counter: Counter;
        }
        test_class_alt() : int {
            u : ValueOrCounter;
            u.value = 77;
            return u.value;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_class_alt");
    REQUIRE(fn);
    REQUIRE(fn() == 77);
}

TEST_CASE("Union with struct and class mixed — access different alts", "[gen][union][class][structs]") {
    auto jit = gen_jit(R"(
        module gen_union_14;
        struct Coords {
            x: int = 0;
            y: int = 0;
        }
        class Label {
            id: int;
            Label() : id(0) {}
            Label(v: int) : id(v) {}
            get_id() : int { return id; }
        }
        union Element {
            coords: Coords;
            label: Label;
            tag: int;
        }
        test_mixed_tag() : int {
            u : Element;
            u.tag = 123;
            return u.tag;
        }
        test_mixed_coords() : int {
            u : Element;
            c : Coords;
            c.x = 5;
            c.y = 8;
            u.coords = c;
            return u.coords.x + u.coords.y;
        }
    )");
    REQUIRE(jit);
    auto fn_tag = jit->lookup_symbol<int(*)()>("test_mixed_tag");
    REQUIRE(fn_tag);
    REQUIRE(fn_tag() == 123);
    auto fn_coords = jit->lookup_symbol<int(*)()>("test_mixed_coords");
    REQUIRE(fn_coords);
    REQUIRE(fn_coords() == 13);
}

TEST_CASE("Union with pointer alternative", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module gen_union_15;
        struct Data {
            val: int = 0;
        }
        union PtrOrVal {
            ptr: Data*;
            val: int;
        }
        test_ptr_alt() : int {
            u : PtrOrVal;
            u.val = 55;
            return u.val;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_ptr_alt");
    REQUIRE(fn);
    REQUIRE(fn() == 55);
}

// ============================================================
// Phase 4: Discriminant update on alternative assignment
// ============================================================

TEST_CASE("Union discriminant updated on alternative switch", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_16;
        union MyUnion {
            first: int;
            second: long;
            third: byte;
        }
        test_switch_disc() : int {
            u : MyUnion;
            u.first = 10;
            u.second = 20;
            u.third = 30;
            // After assigning to third (index 2), accessing first (index 0) should fail
            // but we can verify the last assigned alternative is readable
            return u.third;
        }
    )", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int8_t(*)()>("test_switch_disc");
    REQUIRE(fn);
    REQUIRE(fn() == 30);
}

TEST_CASE("Union discriminant tracks last assigned alternative", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_17;
        union NumVal {
            i: int;
            l: long;
        }
        get_disc(u: NumVal&) : int {
            // Assign to l (index 1) then read back l
            u.l = 99;
            return 1;
        }
        test_disc_track() : long {
            u : NumVal;
            u.i = 5;
            get_disc(u);
            return u.l;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<long(*)()>("test_disc_track");
    REQUIRE(fn);
    REQUIRE(fn() == 99L);
}

TEST_CASE("Union struct assignment updates discriminant", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module gen_union_18;
        struct Vec2 {
            x: int = 0;
            y: int = 0;
        }
        union VecOrInt {
            vec: Vec2;
            val: int;
        }
        test_struct_disc() : int {
            u : VecOrInt;
            v : Vec2;
            v.x = 3;
            v.y = 4;
            u.vec = v;
            // Now switch to the int alternative
            u.val = 42;
            return u.val;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_struct_disc");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

// ============================================================
// Phase 5: Mangling — unions in function signatures
// ============================================================

TEST_CASE("Union passed by value to function", "[gen][union]") {
    auto jit = gen_jit(R"(
        module gen_union_19;
        union IntOrLong {
            i: int;
            l: long;
        }
        use_val(u: IntOrLong) : int {
            return u.i;
        }
        test_by_val() : int {
            u : IntOrLong;
            u.i = 77;
            return use_val(u);
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_by_val");
    REQUIRE(fn);
    REQUIRE(fn() == 77);
}

// ============================================================
// Phase 6: KDI export/import — unions across modules
// ============================================================

TEST_CASE("Union exported to shared library and used from another module", "[gen][union][import]") {
    auto result = build_exec_with_lib(R"(
        module gen_union_20;
        public:
        union IntOrLong {
            i: int;
            l: long;
        }
        get_forty_two() : int {
            u : IntOrLong;
            u.i = 42;
            return u.i;
        }
    )", R"(
        module gen_union_21;
        import gen_union_20;
        main() : int {
            return gen_union_20::get_forty_two();
        }
    )");
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Union passed by reference across modules", "[gen][union][import]") {
    auto result = build_exec_with_lib(R"(
        module gen_union_22;
        public:
        union IntOrLong {
            i: int;
            l: long;
        }
        read_int(u: IntOrLong&) : int {
            return u.i;
        }
    )", R"(
        module gen_union_23;
        import gen_union_22;
        main() : int {
            u : gen_union_22::IntOrLong;
            u.i = 99;
            return gen_union_22::read_int(u);
        }
    )");
    REQUIRE(result.exit_code == 99);
}

// ============================================================
// Imported aggregates whose private nested union owns resources
// ============================================================

static const char* const k_private_union_holder_lib = R"K(
    module gen_union_priv_lib;
    drops: int = 0;
    getDrops(): int { return drops; }
    class Resource {
        public:
        Resource() {}
        ~Resource() { ++drops; }
    }
    struct Holder {
        private:
        union Storage {
            key: Resource!;
            number: int;
        }
        _storage: Storage;
        public:
        Holder() {}
        setKey() { _storage.key = new Resource(); }
    }
)K";

TEST_CASE("Imported struct with a private owner union is cleaned up by its exported destructor",
          "[gen][union][import][owner]") {
    auto result = build_exec_with_lib(k_private_union_holder_lib, R"K(
        module gen_union_priv_app1;
        import gen_union_priv_lib;
        union Slot { number: int; holder: gen_union_priv_lib::Holder; }
        main(): int {
            {
                h: gen_union_priv_lib::Holder;
                h.setKey();
            }
            if (gen_union_priv_lib::getDrops() != 1) return 10 + gen_union_priv_lib::getDrops();
            {
                s: Slot;
                s.holder = gen_union_priv_lib::Holder();
                s.holder.setKey();
            }
            return gen_union_priv_lib::getDrops() == 2 ? 42 : 20 + gen_union_priv_lib::getDrops();
        }
    )K");
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Imported struct with hidden owner state is not bytewise copyable",
          "[gen][union][import][owner]") {
    REQUIRE_THROWS(build_exec_with_lib(k_private_union_holder_lib, R"K(
        module gen_union_priv_app2;
        import gen_union_priv_lib;
        main(): int {
            h: gen_union_priv_lib::Holder;
            h.setKey();
            c: gen_union_priv_lib::Holder = h;
            return 0;
        }
    )K"));
}

TEST_CASE("Copying a union holding an imported struct with hidden owner state is rejected at runtime",
          "[gen][union][import][owner]") {
    auto result = build_exec_with_lib(k_private_union_holder_lib, R"K(
        module gen_union_priv_app3;
        import gen_union_priv_lib;
        union Slot { number: int; holder: gen_union_priv_lib::Holder; }
        main(): int {
            caught: bool = false;
            {
                s: Slot;
                s.holder = gen_union_priv_lib::Holder();
                s.holder.setKey();
                try { c: Slot = s; }
                catch (e: FatalError&) { caught = true; }
            }
            return caught && gen_union_priv_lib::getDrops() == 1 ? 42 : 1;
        }
    )K");
    REQUIRE(result.exit_code == 42);
}

// ============================================================
// Nested unions inside aggregates
// ============================================================

TEST_CASE("Nested union inside struct - basic declaration and usage", "[gen][union][nested]") {
    auto jit = gen_jit(R"(
        module gen_union_24;
        struct Container {
            union Value {
                i: int;
                l: long;
            }
            v: Value;
            tag: int;
        }
        test_nested_union() : int {
            c : Container;
            c.v.i = 42;
            c.tag = 1;
            return c.v.i;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_nested_union");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Nested union inside struct - multiple alternatives", "[gen][union][nested]") {
    auto jit = gen_jit(R"(
        module gen_union_25;
        struct Wrapper {
            union Data {
                x: int;
                y: long;
                z: double;
            }
            data: Data;
        }
        test_int() : int {
            w : Wrapper;
            w.data.x = 7;
            return w.data.x;
        }
        test_long() : long {
            w : Wrapper;
            w.data.y = 123L;
            return w.data.y;
        }
    )");
    REQUIRE(jit);
    auto fn_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(fn_int);
    REQUIRE(fn_int() == 7);
    auto fn_long = jit->lookup_symbol<long(*)()>("test_long");
    REQUIRE(fn_long);
    REQUIRE(fn_long() == 123L);
}

TEST_CASE("Nested union inside class", "[gen][union][nested]") {
    auto jit = gen_jit(R"(
        module gen_union_26;
        class MyClass {
            public:
            union Result {
                value: int;
                error: long;
            }
            result: Result;
            getResult() : int {
                return result.value;
            }
        }
        test_class_nested_union() : int {
            obj : MyClass;
            obj.result.value = 55;
            return obj.getResult();
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_class_nested_union");
    REQUIRE(fn);
    REQUIRE(fn() == 55);
}

TEST_CASE("Nested union type used as function parameter type", "[gen][union][nested]") {
    auto jit = gen_jit(R"(
        module gen_union_27;
        struct Outer {
            union Inner {
                a: int;
                b: long;
            }
        }
        read_inner(u: Outer::Inner&) : int {
            return u.a;
        }
        test_param() : int {
            o : Outer;
            inner : Outer::Inner;
            inner.a = 33;
            return read_inner(inner);
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_param");
    REQUIRE(fn);
    REQUIRE(fn() == 33);
}


// ============================================================
// Phase 7: Union Kind enum and index() intrinsic
// ============================================================

TEST_CASE("union Kind enum basic values", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module gen_union_28;
        union Value {
            i: int;
            d: double;
            s: byte;
        }
        get_first_kind() : int {
            k : int = Value::Kind::i;
            return k;
        }
        get_second_kind() : int {
            k : int = Value::Kind::d;
            return k;
        }
        get_third_kind() : int {
            k : int = Value::Kind::s;
            return k;
        }
    )");
    REQUIRE(jit);
    auto fn0 = jit->lookup_symbol<int(*)()>("get_first_kind");
    auto fn1 = jit->lookup_symbol<int(*)()>("get_second_kind");
    auto fn2 = jit->lookup_symbol<int(*)()>("get_third_kind");
    REQUIRE(fn0);
    REQUIRE(fn1);
    REQUIRE(fn2);
    REQUIRE(fn0() == 0);
    REQUIRE(fn1() == 1);
    REQUIRE(fn2() == 2);
}

TEST_CASE("union index() basic", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module gen_union_29;
        union Value {
            i: int;
            d: double;
        }
        test_index_int() : int {
            v : Value;
            v.i = 42;
            idx : int = v.index();
            return idx;
        }
        test_index_double() : int {
            v : Value;
            v.d = 3.14;
            idx : int = v.index();
            return idx;
        }
    )");
    REQUIRE(jit);
    auto fn_i = jit->lookup_symbol<int(*)()>("test_index_int");
    auto fn_d = jit->lookup_symbol<int(*)()>("test_index_double");
    REQUIRE(fn_i);
    REQUIRE(fn_d);
    REQUIRE(fn_i() == 0);
    REQUIRE(fn_d() == 1);
}

TEST_CASE("union index() after reassignment", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module gen_union_30;
        union Value {
            i: int;
            d: double;
        }
        test_reassign() : int {
            v : Value;
            v.i = 10;
            v.d = 2.5;
            idx : int = v.index();
            return idx;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_reassign");
    REQUIRE(fn);
    REQUIRE(fn() == 1); // last assignment was .d (index 1)
}

TEST_CASE("union index() compared with Kind enum", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module gen_union_31;
        union Value {
            i: int;
            d: double;
            b: bool;
        }
        test_compare() : int {
            v : Value;
            v.d = 1.0;
            if(v.index() == Value::Kind::d) {
                return 1;
            }
            return 0;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_compare");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("union index() on nested union in struct", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module gen_union_32;
        struct Container {
            union Inner {
                a: int;
                b: double;
            }
            val: Inner;
        }
        test_nested_index() : int {
            c : Container;
            c.val.b = 2.0;
            idx : int = c.val.index();
            return idx;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_nested_index");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("template union Kind enum and index()", "[gen][union][kind][template]") {
    auto jit = gen_jit(R"(
        module gen_union_33;
        template<typename T>
        union MaybeVal {
            some: T;
            none: byte;
        }
        test_optional_some() : int {
            o : MaybeVal<int>;
            o.some = 42;
            idx : int = o.index();
            return idx;
        }
        test_optional_none() : int {
            o : MaybeVal<int>;
            o.none = 0;
            idx : int = o.index();
            return idx;
        }
    )");
    REQUIRE(jit);
    auto fn_some = jit->lookup_symbol<int(*)()>("test_optional_some");
    auto fn_none = jit->lookup_symbol<int(*)()>("test_optional_none");
    REQUIRE(fn_some);
    REQUIRE(fn_none);
    REQUIRE(fn_some() == 0);
    REQUIRE(fn_none() == 1);
}

TEST_CASE("template struct with nested union — index and member access", "[gen][union][template][nested]") {
    auto jit = gen_jit(R"(
        module gen_union_34;
        template<typename R, typename E>
        struct Expected {
            union Storage {
                result: R;
                error: E;
            }
            _storage : Storage;

            hasResult() : bool {
                return _storage.index() == Storage::Kind::result;
            }
            setResult(value : R&) {
                _storage.result = value;
            }
            getResult() : R {
                return _storage.result;
            }
        }

        test() : int {
            e : Expected<int, int>;
            e.setResult(42);
            if (e.hasResult()) {
                return e.getResult();
            }
            return 0;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("template struct with nested union — factory function", "[gen][union][template][nested][factory]") {
    auto jit = gen_jit(R"(
        module gen_union_35;
        template<typename R, typename E>
        struct Expected {
            union Storage {
                result: R;
                error: E;
            }
            _storage : Storage;

            hasResult() : bool {
                return _storage.index() == Storage::Kind::result;
            }
            setResult(value : R&) {
                _storage.result = value;
            }
            getResult() : R {
                return _storage.result;
            }

            static expected(value : R&) : Expected<R, E> {
                e : Expected<R, E>;
                e.setResult(value);
                return e;
            }
        }

        test() : int {
            e : Expected<int, int> = Expected<int, int>::expected(42);
            if (e.hasResult()) {
                return e.getResult();
            }
            return 0;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}



// Regression: assigning an owner-array alternative to a union must update the
// discriminant. Previously the owner-assignment codegen path returned early,
// before the discriminant update, leaving the union mistagged (and crashing on
// the next read).
TEST_CASE("Union owner-array alternative updates discriminant", "[gen][union][owner]") {
    auto jit = gen_jit(R"(
        module gen_union_36;
        union Store {
            u8  : unsigned byte[]!;
            u16 : unsigned short[]!;
            u32 : char[]!;
        }
        // index() after assigning the third (owner-array) alternative must be 2.
        test_index() : int {
            s : Store;
            s.u32 = new char[3];
            return (int) s.index();
        }
        // The stored content must be readable through the active alternative.
        test_value() : int {
            s : Store;
            s.u32 = new char[3];
            s.u32[0] = 'A';
            return (int) s.u32[0];
        }
        // Switching alternatives must re-tag the discriminant.
        test_switch() : int {
            s : Store;
            s.u32 = new char[2];
            s.u8 = new unsigned byte[4];
            return (int) s.index();   // now UTF-8 alternative → index 0
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("test_index")() == 2);
    CHECK(jit->lookup_symbol<int(*)()>("test_value")() == 65);
    CHECK(jit->lookup_symbol<int(*)()>("test_switch")() == 0);
}

TEST_CASE("Default first struct alternative owners written through references are released",
          "[gen][union][default][struct][owner][reference][lifecycle][regression]") {
    auto jit = gen_jit(R"K(
        module union_default_struct_owners;
        drops: int = 0;
        class Resource { public: Resource() {} ~Resource() { ++drops; } }
        struct Pair { a: Resource!; b: Resource!; }
        union U { pair: Pair; number: int; }
        borrow(u: U&): Pair& { return u.pair; }
        viaReference(): int {
            drops = 0;
            {
                u: U;
                p: Pair& = borrow(u);
                p.a = new Resource();
                p.b = new Resource();
            }
            return drops == 2 ? 42 : -(100 + drops);
        }
        viaDirectAccess(): int {
            drops = 0;
            {
                u: U;
                u.pair.a = new Resource();
            }
            return drops == 1 ? 42 : -(200 + drops);
        }
        replaced(): int {
            drops = 0;
            {
                u: U;
                borrow(u).a = new Resource();
                u.number = 7;
                if (drops != 1) return -(300 + drops);
            }
            return drops == 1 ? 42 : -(400 + drops);
        }
        untouched(): int {
            drops = 0;
            {
                u: U;
                copy: U = u;
                if (copy.index() != 0) return -1;
            }
            return drops == 0 ? 42 : -(500 + drops);
        }
    )K");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int (*)()>("viaReference")() == 42);
    CHECK(jit->lookup_symbol<int (*)()>("viaDirectAccess")() == 42);
    CHECK(jit->lookup_symbol<int (*)()>("replaced")() == 42);
    CHECK(jit->lookup_symbol<int (*)()>("untouched")() == 42);
}

TEST_CASE("Copying a default first struct alternative holding owners is rejected",
          "[gen][union][default][struct][owner][copy][exceptions][regression]") {
    auto jit = gen_jit(R"K(
        module union_default_struct_copy;
        drops: int = 0;
        class Resource { public: Resource() {} ~Resource() { ++drops; } }
        struct Pair { a: Resource!; }
        union U { pair: Pair; number: int; }
        struct Wrapper { value: U; tag: int; }
        borrow(u: U&): Pair& { return u.pair; }
        direct(): int {
            drops = 0;
            code: int = 0;
            {
                u: U;
                borrow(u).a = new Resource();
                try { c: U = u; }
                catch (error: FatalError&) { code = error.getCode(); }
            }
            return code == 0x55434f50 && drops == 1 ? 42 : -(100 + drops);
        }
        nested(): int {
            drops = 0;
            code: int = 0;
            {
                w: Wrapper;
                borrow(w.value).a = new Resource();
                try { c: Wrapper = w; }
                catch (error: FatalError&) { code = error.getCode(); }
            }
            return code == 0x55434f50 && drops == 1 ? 42 : -(200 + drops);
        }
        assignment(): int {
            drops = 0;
            code: int = 0;
            {
                source: U;
                borrow(source).a = new Resource();
                target: U;
                try { target = source; }
                catch (error: FatalError&) { code = error.getCode(); }
                if (target.index() != -1 && target.index() != 0) return -1;
            }
            return code == 0x55434f50 && drops == 1 ? 42 : -(300 + drops);
        }
    )K");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int (*)()>("direct")() == 42);
    CHECK(jit->lookup_symbol<int (*)()>("nested")() == 42);
    CHECK(jit->lookup_symbol<int (*)()>("assignment")() == 42);
}

TEST_CASE("Default first class alternative owners written through references are released",
          "[gen][union][default][class][owner][reference][lifecycle][regression]") {
    auto jit = gen_jit(R"K(
        module union_default_class_owners;
        drops: int = 0;
        class Resource { public: Resource() {} ~Resource() { ++drops; } }
        class Holder { public: owned: Resource!; Holder() {} }
        union U { holder: Holder; number: int; }
        borrow(u: U&): Holder& { return u.holder; }
        test(): int {
            drops = 0;
            {
                u: U;
                h: Holder& = borrow(u);
                h.owned = new Resource();
            }
            return drops == 1 ? 42 : -(100 + drops);
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Default first struct alternative with nested owner structs releases every field once",
          "[gen][union][default][struct][nested][owner][lifecycle][regression]") {
    auto jit = gen_jit(R"K(
        module union_default_nested_struct_owners;
        drops: int = 0;
        class Resource { public: Resource() {} ~Resource() { ++drops; } }
        struct Inner { a: Resource!; }
        struct Outer { inner: Inner; b: Resource!; count: int; }
        union U { outer: Outer; number: int; }
        borrow(u: U&): Outer& { return u.outer; }
        test(): int {
            drops = 0;
            {
                u: U;
                o: Outer& = borrow(u);
                o.inner.a = new Resource();
                o.b = new Resource();
                o.count = 2;
            }
            return drops == 2 ? 42 : -(100 + drops);
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Default first trivial struct alternative is still copied by value",
          "[gen][union][default][struct][copy][trivial][regression]") {
    auto jit = gen_jit(R"K(
        module union_default_trivial_struct;
        struct Plain { a: int; b: int; }
        union U { plain: Plain; number: int; }
        borrow(u: U&): Plain& { return u.plain; }
        peek(u: const U&): const Plain& { return u.plain; }
        test(): int {
            u: U;
            p: Plain& = borrow(u);
            p.a = 40;
            p.b = 2;
            copy: U = u;
            if (copy.index() != 0) return -1;
            q: const Plain& = peek(copy);
            return q.a + q.b;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Default first owner array alternative element writes are released",
          "[gen][union][default][array][owner][lifecycle][regression]") {
    auto jit = gen_jit(R"K(
        module union_default_array_owners;
        drops: int = 0;
        class Resource { public: Resource() {} ~Resource() { ++drops; } }
        union U { items: Resource![2]; number: int; }
        test(): int {
            drops = 0;
            {
                u: U;
                u.items[1] = new Resource();
            }
            return drops == 1 ? 42 : -(100 + drops);
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}
