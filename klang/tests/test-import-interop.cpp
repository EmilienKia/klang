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
 * Integration tests for aliases, typedefs, callables and cross-module interop.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/context.hpp"
#include "../src/model/tools/kdi_exporter.hpp"

#include <llvm/IR/Module.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>

namespace fs = std::filesystem;

namespace {

/// Library exporting a soft alias, two typedefs and a function typed with one
/// of the typedefs.
constexpr const char* ALIAS_LIB_SRC = R"K(
    module import_interop_01;

    typedef Identifier : int;
    alias   Num        : int;

    struct Point { x : int; y : int; }
    typedef Coord : Point;

    makeId(v : Identifier) : Identifier { return v; }
)K";

} // anonymous namespace

namespace {
// A single i32 field uses the same native return register as LLVM { i32 }.
struct ImportedNativeCallableResult { int value; };
}

extern "C" ImportedNativeCallableResult __k_test_import_callable_native_free(int a, int b) {
    return {a * 10 + b};
}

extern "C" ImportedNativeCallableResult __k_test_import_callable_native_static(int a, int b) {
    return {a + b * 10};
}

TEST_CASE("KDI imported native aggregate-return callables use the K ABI",
          "[import][kdi][callable][ffi][native-aggregate]") {
    auto producer = compile_model_with_stdlib(R"K(
        module kdi_callable_native_result;
        public struct Result { value : int; }
        @ffi::Extern("C", null, "__k_test_import_callable_native_free")
        public nativeFree(a : int, b : int) : Result;
        public struct Native {
            @ffi::Extern("C", null, "__k_test_import_callable_native_static")
            public static make(a : int, b : int) : Result;
        }
        public makeK(a : int, b : int) : Result {
            result : Result;
            result.value = a * 10 + b;
            return result;
        }
    )K");
    REQUIRE(producer);
    auto make_k = producer->get_unit()->get_root_namespace()->get_function("makeK");
    REQUIRE(make_k);
    const auto make_k_symbol = make_k->get_mangled_name();
    k::model::kdi_builder exporter(*producer->get_context_for_test());
    producer->get_unit()->accept(exporter);
    auto descriptor = exporter.take();
    auto path = fs::current_path() / "kdi_callable_native_result.kdi";
    REQUIRE_FALSE(fs::exists(path));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove(path, ec); }
    } cleanup{path};
    REQUIRE(kdi::kdi_write_cbor_file(descriptor, path.string()));

    auto producer_jit = producer->to_jit();
    REQUIRE(producer_jit);
    auto make_k_address = producer_jit->lookup_symbol<void (*)()>("makeK");
    REQUIRE(make_k_address);
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("kdi_callable_native_result", path);
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto consumer = k::compiler::create();
    consumer->set_file_resolver(resolver);
    REQUIRE_NOTHROW(consumer->parse_source("kdi_callable_native_consumer.k", R"K(
        module kdi_callable_native_consumer;
        import kdi_callable_native_result;
        run() : int {
            freeFn : *(int, int):Result = kdi_callable_native_result::nativeFree;
            freeFnAgain : *(int, int):Result = kdi_callable_native_result::nativeFree;
            staticFn : *(int, int):Result = kdi_callable_native_result::Native::make;
            kFn : *(int, int):Result = kdi_callable_native_result::makeK;
            a : Result = freeFn(4, 2);
            again : Result = freeFnAgain(4, 2);
            b : Result = staticFn(2, 4);
            c : Result = kFn(4, 2);
            d : Result = kdi_callable_native_result::nativeFree(4, 2);
            e : Result = kdi_callable_native_result::Native::make(2, 4);
            return a.value + again.value + b.value + c.value + d.value + e.value;
        }
    )K", false, false));
    auto& llvm_module = consumer->get_context_for_test()->module();
    auto* native_free = llvm_module.getFunction("__k_test_import_callable_native_free");
    auto* native_static = llvm_module.getFunction("__k_test_import_callable_native_static");
    auto* k_function = llvm_module.getFunction(make_k_symbol);
    REQUIRE(native_free);
    REQUIRE(native_static);
    REQUIRE(k_function);
    CHECK(native_free->getReturnType()->isStructTy());
    CHECK(native_static->getReturnType()->isStructTy());
    CHECK(k_function->getReturnType()->isVoidTy());
    CHECK(native_free->arg_size() == 2);
    CHECK(native_static->arg_size() == 2);
    CHECK(k_function->arg_size() == 3);
    unsigned adapters = 0;
    for (auto& fn : llvm_module) {
        auto attr = fn.getFnAttribute("k.native_callable_target");
        if (!attr.isStringAttribute()) continue;
        ++adapters;
        CHECK((attr.getValueAsString() == native_free->getName()
            || attr.getValueAsString() == native_static->getName()));
    }
    CHECK(adapters == 2);
    REQUIRE(k_function->isDeclaration());
    REQUIRE(bind_jit_fixture(k_function, reinterpret_cast<void*>(make_k_address)));
    auto jit = consumer->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    REQUIRE(run);
    CHECK(run() == 252);

    auto legacy = std::find_if(descriptor.unit.root_ns.functions.begin(),
        descriptor.unit.root_ns.functions.end(),
        [](const auto& fn) { return fn.name == "nativeFree"; });
    REQUIRE(legacy != descriptor.unit.root_ns.functions.end());
    legacy->is_extern = false;
    legacy->llvm_def = "declare { i32 } @__k_test_import_callable_native_free(i32, i32)";
    REQUIRE(kdi::kdi_write_cbor_file(descriptor, path.string()));
    auto legacy_resolver = std::make_shared<k::path_lookup_file_resolver>();
    legacy_resolver->add_explicit_path("kdi_callable_native_result", path);
    legacy_resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto legacy_consumer = k::compiler::create();
    legacy_consumer->set_file_resolver(legacy_resolver);
    CHECK_THROWS_WITH(legacy_consumer->parse_source("kdi_callable_legacy_consumer.k", R"K(
        module kdi_callable_legacy_consumer;
        import kdi_callable_native_result;
        run() : int {
            f : *(int, int):Result = kdi_callable_native_result::nativeFree;
            result : Result = f(4, 2);
            return result.value;
        }
    )K", false, false), Catch::Matchers::ContainsSubstring("not K sret"));
}

// ─────────────────────────────────────────────────────────────────────────────
// An imported soft alias, typedef and typedef-over-struct are all usable as
// types from the importing module.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("import exported aliases and typedefs", "[import][alias][typedef]") {
    auto result = build_exec_with_lib(
        ALIAS_LIB_SRC,
        R"K(
            module import_interop_02;
            import import_interop_01;

            main() : int {
                id : import_interop_01::Identifier = 4;
                n  : import_interop_01::Num        = 3;
                c  : import_interop_01::Coord;
                c.x = 5;
                return import_interop_01::makeId(id) + n + c.x;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 12 );
}

// ─────────────────────────────────────────────────────────────────────────────
// An imported typedef keeps its nominal identity: assigning the underlying
// type to it still requires an explicit cast, and the cast is a no-op.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("imported typedef keeps its nominal identity", "[import][typedef]") {
    auto result = build_exec_with_lib(
        ALIAS_LIB_SRC,
        R"K(
            module import_interop_03;
            import import_interop_01;

            main() : int {
                n  : int = 40;
                id : import_interop_01::Identifier = (import_interop_01::Identifier) n;
                // The reverse direction is implicit.
                back : int = id;
                return back + 2;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// An imported soft alias is fully transparent: it is interchangeable with the
// type it renames, in both directions and without any cast.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("imported soft alias is transparent", "[import][alias]") {
    auto result = build_exec_with_lib(
        ALIAS_LIB_SRC,
        R"K(
            module import_interop_04;
            import import_interop_01;

            twice(v : int) : int { return v * 2; }

            main() : int {
                n : import_interop_01::Num = 21;
                m : int = n;
                return twice(m);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameterised aliases (template<typename T> alias / typedef) across modules.
//
// A parameterised alias denotes no type by itself, so it is round-tripped
// through KDI as source text and re-parsed by the importing compiler, exactly
// like a template definition. The consumer then substitutes its own arguments.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Imported outer templates retain nested specialization identity",
          "[import][e2e][nested-template-descriptor]") {
    auto result = build_exec_with_lib(
        R"K(
            module nested_template_descriptor_lib;
            public:
            template<typename T>
            struct Host {
                public:
                template<typename U> static struct Inner {
                    public: outer : T; own : U;
                }
                roundtrip(value : T) : T {
                    item : Inner<int>;
                    item.outer = value;
                    return item.outer;
                }
            }
        )K",
        R"K(
            module nested_template_descriptor_app;
            import nested_template_descriptor_lib;
            main() : int {
                small : Host<int>;
                large : Host<long>;
                return small.roundtrip(21) + (int)large.roundtrip(21L);
            }
        )K");
    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Imported outer templates retain nested specialization with an enclosing using alias",
          "[import][e2e][nested-template-descriptor][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module nested_template_alias_descriptor_lib;
            namespace alias_types {
                struct Marker { value : int; }
            }
            using MarkerAlias = alias_types::Marker;

            public:
            template<typename T>
            struct Host {
                public:
                template<typename U> static struct Inner {
                    public: outer : T; own : U;
                }
                roundtrip(value : T) : T {
                    item : Inner<int>;
                    item.outer = value;
                    return item.outer;
                }
            }
        )K",
        R"K(
            module nested_template_alias_descriptor_app;
            import nested_template_alias_descriptor_lib;
            main() : int {
                small : Host<int>;
                large : Host<long>;
                return small.roundtrip(21) + (int)large.roundtrip(21L);
            }
        )K");
    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Reconstructed nested templates preserve binders and defaults",
          "[import][e2e][nested-template-descriptor][using-alias][defaults]") {
    auto result = build_exec_with_lib(
        R"K(
            module nested_template_alias_defaults_lib;
            namespace alias_types { struct Marker { value : int; } }
            using T = alias_types::Marker;

            public:
            template<typename T>
            struct Host {
                public:
                template<typename T> static struct Inner {
                    public: value : T;
                }
                template<typename U = T, int N = 20 + 22> static struct Default {
                    public: value : U;
                    number() : int { return N; }
                }
                run() : int {
                    item : Inner<long>;
                    item.value = 19L;
                    fallback : Default<>;
                    fallback.value = 23;
                    if (fallback.number() != 42) return -1;
                    return (int)item.value + fallback.value;
                }
            }
        )K",
        R"K(
            module nested_template_alias_defaults_app;
            import nested_template_alias_defaults_lib;
            main() : int {
                host : Host<int>;
                return host.run();
            }
        )K");
    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Reconstructed nested templates preserve aliased arguments and member templates",
          "[import][e2e][nested-template-descriptor][using-alias][member-template]") {
    auto result = build_exec_with_lib(
        R"K(
            module nested_template_alias_members_lib;
            namespace alias_types { struct Marker { value : int; } }
            using MarkerAlias = alias_types::Marker;

            public:
            template<typename T>
            struct Host {
                public:
                template<typename U> static struct Cell { public: item : U; }
                template<typename U> struct Inner {
                    public:
                    own : int;
                    template<typename V> Inner(v : V) : own(v) {}
                    static sum(a : int, b : int) : int { return a + b; }
                    template<typename... Args>
                    add(Args... args) : int { return sum(args...); }
                }
                run() : int {
                    cell : Cell<MarkerAlias>;
                    cell.item.value = 2;
                    inner : Inner<T>(40);
                    return inner.add<int, int>(inner.own, cell.item.value);
                }
            }
        )K",
        R"K(
            module nested_template_alias_members_app;
            import nested_template_alias_members_lib;
            main() : int {
                host : Host<int>;
                return host.run();
            }
        )K");
    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Imported template source preserves friend declarations",
          "[import][kdi][nested-template-descriptor][using-alias][friend]") {
    auto source = std::string(R"K(
        module nested_template_alias_friend_lib;
        namespace alias_types { struct Marker { value : int; } }
    )K");
    SECTION("raw-source control") {}
    SECTION("enclosing using alias") {
        source += "using MarkerAlias = alias_types::Marker;\n";
    }
    source += R"K(
        struct FriendType {}
        template<typename T> struct Host {
            friend FriendType;
            template<typename U> static struct Inner { public: value : U; }
        }
    )K";
    auto producer = compile_model_with_stdlib(source);
    REQUIRE(producer);
    k::model::kdi_builder exporter(*producer->get_context_for_test());
    REQUIRE_NOTHROW(producer->get_unit()->accept(exporter));
    auto file = exporter.take();
    auto entry = std::find_if(file.unit.root_ns.template_defs.begin(),
        file.unit.root_ns.template_defs.end(),
        [](const auto& definition) { return definition.name == "Host"; });
    REQUIRE(entry != file.unit.root_ns.template_defs.end());
    CHECK(entry->source.find("friend FriendType;") != std::string::npos);
    CHECK(entry->source.find("template<typename U> static struct Inner") != std::string::npos);
}

TEST_CASE("cross-module parameterised alias — soft alias of a template aggregate",
          "[import][e2e][alias][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module import_interop_05;

            public:
            template<typename T>
            struct Box {
                v : T;
            }

            template<typename T> alias BoxOf : Box<T>;
            template<typename T> alias Ptr : T*;

            dummy() : int { return 0; }
        )K",
        R"K(
            module import_interop_06;
            import import_interop_05;

            main() : int {
                b : BoxOf<int>;
                b.v = 40;
                p : Ptr<int> = &b.v;
                return *p + 2;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

TEST_CASE("cross-module parameterised alias — strong alias keeps its nominal identity",
          "[import][e2e][typedef][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module import_interop_07;

            public:
            template<typename T> typedef Id : T;

            dummy() : int { return 0; }
        )K",
        R"K(
            module import_interop_08;
            import import_interop_07;

            main() : int {
                a : Id<int> = 40;
                n : int = 2;
                b : Id<int> = (Id<int>)n;
                return (int)a + (int)b;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ═════════════════════════════════════════════════════════════════════════════
// Callable types across module boundaries (KDI export/import)
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// A library exporting a callable-typed global, a callable parameter and a
// callable return type.  All three must round-trip through the .kdi descriptor
// and stay usable (and callable) from the importing executable.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — global, parameter and return type round-trip",
          "[import][e2e][callable]") {
    auto result = build_exec_with_lib(
        R"K(
            module import_interop_09;

            public isPos(x: int) : bool { return x > 0; }

            public gPred : *(int):bool = isPos;

            public applyPred(p: *(int):bool, v: int) : bool { return p(v); }

            public getPred() : *(int):bool { return isPos; }
        )K",
        R"K(
            module import_interop_10;
            import import_interop_09;

            isNeg(x: int) : bool { return x < 0; }

            main() : int {
                // Callable-typed global imported from the library.
                g : *(int):bool = import_interop_09::gPred;
                if (!g(7)) return 1;
                // Callable return type.
                q : *(int):bool = import_interop_09::getPred();
                if (!q(4)) return 2;
                // Callable parameter, bound to a local function.
                if (!import_interop_09::applyPred(isNeg, -1)) return 3;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// A soft alias over a callable is a pure renaming: the importing module may use
// the alias name or the underlying callable interchangeably.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — soft alias over a callable is transparent",
          "[import][e2e][callable][alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module import_interop_11;

            public isPos(x: int) : bool { return x > 0; }

            public alias Pred : *(int):bool;

            public applyAlias(p: Pred, v: int) : bool { return p(v); }
        )K",
        R"K(
            module import_interop_12;
            import import_interop_11;

            main() : int {
                p : import_interop_11::Pred = import_interop_11::isPos;
                if (!p(3)) return 1;
                // The alias and the raw callable denote the very same type.
                r : *(int):bool = p;
                if (!r(5)) return 2;
                if (!import_interop_11::applyAlias(import_interop_11::isPos, 9)) return 3;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// A typedef over a callable keeps its nominal identity across the module
// boundary: converting the underlying callable into it requires an explicit cast.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — typedef over a callable stays nominal",
          "[import][e2e][callable][typedef]") {
    auto result = build_exec_with_lib(
        R"K(
            module import_interop_13;

            public isPos(x: int) : bool { return x > 0; }

            public typedef StrictPred : *(int):bool;

            public getPred() : *(int):bool { return isPos; }
        )K",
        R"K(
            module import_interop_14;
            import import_interop_13;

            main() : int {
                s : import_interop_13::StrictPred =
                    (import_interop_13::StrictPred) import_interop_13::getPred();
                return s(2) ? 42 : 1;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// A parameterised alias over a callable is exported as source text and
// re-materialised on import; instantiating it in the consumer must produce the
// very same callable type as the raw prototype (this is what `k::functional`
// relies on).  Imported parameterised aliases are re-homed under the consumer's
// root namespace, so they are referenced unqualified.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — parameterised alias over a callable round-trips",
          "[import][e2e][callable][alias][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module import_interop_15;

            public:
            template<typename T, typename R> alias Fn : *(T):R;
            template<typename T> alias Predicate : Fn<T,bool>;

            isPos(x: int) : bool { return x > 0; }

            applyFn(p: Fn<int,bool>, v: int) : bool { return p(v); }
        )K",
        R"K(
            module import_interop_16;
            import import_interop_15;

            isNeg(x: int) : bool { return x < 0; }

            main() : int {
                f : Fn<int,bool> = isNeg;
                if (!f(-5)) return 1;
                // Alias chaining: Predicate<T> renames Fn<T,bool>.
                p : Predicate<int> = import_interop_15::isPos;
                if (!p(3)) return 2;
                // The instantiated alias is the raw callable type.
                r : *(int):bool = f;
                if (!r(-9)) return 3;
                if (!import_interop_15::applyFn(import_interop_15::isPos, 9)) return 4;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Callable mangling must be stable across the export/import boundary: the
// symbol the consumer emits for a call has to match the one the library
// defined, including the addresser and the declared throws set.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — mangling stays stable across import",
          "[import][e2e][callable][mangling]") {
    auto result = build_exec_with_lib(
        R"K(
            module import_interop_17;

            public class Boom : public Throwable {
                public Boom() {}
            }

            public byPtr(f: *(int):int, v: int) : int { return f(v); }
            public byRef(f: &(int):int, v: int) : int { return f(v); }
            public byLink(f: +(int):int, v: int) : int { return f(v); }
            public byView(f: ?(int):int, v: int) : int { return f(v); }
            // The throws clause of a callable parameter type is greedy, so such a
            // parameter must come last (see TODO.md, "callable throws clause in a
            // parameter list").
            public withThrows(v: int, f: *(int):int throws(Boom)) : int throws(Boom) { return f(v); }
        )K",
        R"K(
            module import_interop_18;
            import import_interop_17;

            twice(x: int) : int { return x * 2; }

            main() : int {
                acc : int = 0;
                acc += import_interop_17::byPtr(twice, 1);
                acc += import_interop_17::byRef(twice, 2);
                acc += import_interop_17::byLink(twice, 3);
                acc += import_interop_17::byView(twice, 4);
                try {
                    acc += import_interop_17::withThrows(11, twice);
                } catch (e : import_interop_17::Boom&) {
                    return 5;
                }
                return acc;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

TEST_CASE("import cast-operator — explicit cast works on imported class value",
          "[import][e2e][cast][operator]") {
    auto result = build_exec_with_lib(
        R"K(
            module import_interop_19;

            public class Wrapper {
                public value : int;
                public Wrapper(v : int) : value(v) {}
                public operator() : int { return value; }
            }

            public make(v : int) : Wrapper {
                return Wrapper(v);
            }
        )K",
        R"K(
            module import_interop_20;
            import import_interop_19;

            main() : int {
                return (int) import_interop_19::make(42);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("import cast-operator — unsigned conversions keep distinct types and symbols",
          "[import][e2e][cast][operator][regression]") {
    auto result = build_exec_with_lib(
        R"K(
            module import_unsigned_cast_library;
            public class Number {
                public Number() {}
                public const operator() : int { return -1; }
                public const operator() : unsigned short { return (unsigned short) 16; }
                public const operator() : unsigned int { return (unsigned int) 32; }
                public const operator() : unsigned long { return (unsigned long) 64; }
            }
        )K",
        R"K(
            module import_unsigned_cast_consumer;
            import import_unsigned_cast_library;
            main() : int {
                value : import_unsigned_cast_library::Number;
                if ((int) value != -1) return 1;
                if ((unsigned short) value != 16) return 2;
                if ((unsigned int) value != 32) return 3;
                if ((unsigned long) value != 64) return 4;
                return 0;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-alias-no-reexport] Regression: Imported aliases from auto-imported `k`
// (e.g. k::functional::Function/Consumer/Predicate) and from explicit dependency
// libraries must not be re-exported into downstream library KDIs, and multiple
// libraries importing them must not produce duplicate alias declarations.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("import multi-lib — imported aliases do not collide across dependency chain",
          "[import][e2e][alias][regression]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module import_interop_21;
            public template<typename T> alias Pred : *(T):bool;
            public isEven(x: int) : bool { return x % 2 == 0; }
        )K" },
        { R"K(
            module import_interop_22;
            import import_interop_21;
            public checkVal(p: import_interop_21::Pred<int>, v: int) : bool {
                return p(v);
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module import_interop_23;
            import import_interop_21;
            import import_interop_22;

            main() : int {
                if (!import_interop_22::checkVal(import_interop_21::isEven, 42)) return 1;
                if (import_interop_22::checkVal(import_interop_21::isEven, 43)) return 2;
                return 0;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

