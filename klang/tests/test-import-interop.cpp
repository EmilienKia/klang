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
#include "../src/errors.hpp"
#include "../src/model/context.hpp"
#include "../src/model/tools/kdi_exporter.hpp"

#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/IRBuilder.h>

#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>

namespace fs = std::filesystem;

namespace {

constexpr const char* REDIRECT_LIB_SRC = R"K(
    module redirect_import_provider;
    public:
    struct Prefix { padding : long = 91; }
    struct Base {
        value : int = 37;
        body(offset : int) : int { return value + offset; }
        freeBody(offset : int) : int -> baseFree;
        const constBody(offset : int) : int { return value + offset; }
    }
    baseFree(object : Base&, offset : int) : int { return object.value + offset; }
    struct Derived : public Prefix, public Base {
        read(renamed : int = 5) : int -> Base::body;
        viaFree(renamed : int = 3) : int -> Base::freeBody;
        const constRead(renamed : int = 1) : int -> Base::constBody;
    }
    freeAlias(object : Derived&, renamed : int = 7) : int -> Derived::read;
    chain(object : Derived&, renamed : int = 9) : int -> Derived::viaFree;
)K";

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

TEST_CASE("redirect KDI separates source signatures and actual data-alias providers",
          "[import][redirect-contract][regression]") {
    auto compiler = k::compiler::create(make_pic_target_machine());
    compiler->parse_source("redirect-import-provider.k", REDIRECT_LIB_SRC, false, false);
    auto context = compiler->get_context_for_test();
    auto unit = compiler->get_unit();
    k::model::kdi_builder builder(*context);
    unit->accept(builder);
    auto file = builder.take();
    REQUIRE(kdi::kdi_validate(file).is_valid());
    const auto& functions = file.unit.root_ns.functions;
    auto chain = std::find_if(functions.begin(), functions.end(), [](const auto& fn) {
        return fn.name == "chain";
    });
    REQUIRE(chain != functions.end());
    REQUIRE(chain->redirect_target);
    CHECK_FALSE(chain->redirect_target->is_member);
    REQUIRE(chain->redirect_target->receiver_type);
    CHECK(kdi::kdi_type_to_string(*chain->redirect_target->receiver_type)
          .find("redirect_import_provider::Base") != std::string::npos);
    REQUIRE(chain->params.size() == 2);
    CHECK(chain->params[1].name == "renamed");
    REQUIRE(chain->params[1].default_source);
    CHECK(*chain->params[1].default_source == "9");
    CHECK(chain->mangled_name != chain->redirect_target->symbol);
    CHECK(chain->llvm_def.find("@" + chain->mangled_name + "(") != std::string::npos);
    auto alias = context->module().getNamedAlias(chain->mangled_name);
    REQUIRE(alias);
    auto provider = context->module().getFunction(chain->redirect_target->symbol);
    REQUIRE(provider);
    CHECK(alias->getAliaseeObject() == provider);
}

TEST_CASE("imported provider prototypes preserve calling convention and ABI attributes",
          "[import][redirect-contract][regression]") {
    auto context = k::model::context::create();
    context->init_module("redirect_import_provider_abi");
    auto result_type = llvm::StructType::create(context->llvm_context(),
        {llvm::Type::getInt32Ty(context->llvm_context())}, "redirect_import_result");
    const std::string symbol = "redirect_import_provider_abi";
    const std::string prototype = "declare fastcc void @" + symbol
        + "(ptr noalias sret(%redirect_import_result) align 16,"
          " ptr byval({ i64 }) align 8, i8 zeroext)";
    auto native = context->declare_llvm_function_from_def(prototype, symbol);
    REQUIRE(native);
    CHECK(native->getCallingConv() == llvm::CallingConv::Fast);
    CHECK(native->hasParamAttribute(0, llvm::Attribute::StructRet));
    CHECK(native->getParamStructRetType(0) == result_type);
    CHECK(native->hasParamAttribute(0, llvm::Attribute::NoAlias));
    CHECK(native->getParamAlign(0).valueOrOne().value() == 16);
    CHECK(native->hasParamAttribute(1, llvm::Attribute::ByVal));
    CHECK(native->getParamAlign(1).valueOrOne().value() == 8);
    CHECK(native->hasParamAttribute(2, llvm::Attribute::ZExt));
    CHECK(context->declare_llvm_function_from_def(prototype, symbol) == native);
    CHECK(context->module().getFunction(symbol + ".1") == nullptr);
    CHECK(context->declare_llvm_function_from_def(
        "declare void @" + symbol + "(ptr, ptr, i8)", symbol) == nullptr);
    CHECK_FALSE(llvm::verifyModule(context->module(), &llvm::errs()));
}

TEST_CASE("imported provider prototypes share the compiler callable layout",
          "[import][callable][regression]") {
    auto context = k::model::context::create();
    context->init_module("callable_import_layout");
    const std::string symbol = "callable_import_layout_provider";
    // The provider prototype is parsed before any K callable type is used.
    auto native = context->declare_llvm_function_from_def(
        "%__k.callable = type { ptr, ptr }\ndeclare void @" + symbol + "(%__k.callable)", symbol);
    REQUIRE(native);
    auto* callable = context->get_or_create_callable_llvm_type();
    REQUIRE(native->getFunctionType()->getParamType(0) == callable);

    auto& llvm_context = context->llvm_context();
    auto* caller = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_context), false),
        llvm::GlobalValue::ExternalLinkage, "callable_import_layout_caller", &context->module());
    llvm::IRBuilder<> builder(llvm::BasicBlock::Create(llvm_context, "entry", caller));
    builder.CreateCall(native, {builder.CreateLoad(callable, builder.CreateAlloca(callable))});
    builder.CreateRetVoid();
    CHECK_FALSE(llvm::verifyModule(context->module(), &llvm::errs()));
}

namespace {
/// Exposes the compiler-only LLVM context transfer to the reset regression.
struct transferable_context : k::model::context {
    using k::model::context::move_llvm_context;
};
}

TEST_CASE("context reset recreates the callable layout in the new LLVM context",
          "[model][callable][regression]") {
    std::unique_ptr<llvm::LLVMContext> previous;
    transferable_context context;
    context.get_or_create_callable_llvm_type();
    previous = context.move_llvm_context();
    CHECK(&context.get_or_create_callable_llvm_type()->getContext() == &context.llvm_context());
}

TEST_CASE("imported redirect calls project implicit and explicit receiver bindings",
          "[import][redirect-contract][e2e][regression]") {
    auto result = build_exec_with_lib(REDIRECT_LIB_SRC, R"K(
        module redirect_import_consumer;
        import redirect_import_provider;
        localAlias(object : redirect_import_provider::Derived&, offset : int = 11) : int
            -> redirect_import_provider::Derived::read;
        localChain(object : redirect_import_provider::Derived&, offset : int = 13) : int
            -> redirect_import_provider::chain;
        alias Renamed : redirect_import_provider::chain;
        callMember(callback : redirect_import_provider::Base::*(int):int,
                   object : redirect_import_provider::Base&, offset : int) : int {
            return (object.*callback)(offset);
        }
        returnMember() : redirect_import_provider::Base::*(int):int {
            return redirect_import_provider::Derived::read;
        }
        main() : int {
            object : redirect_import_provider::Derived;
            if (object.read() != 42) return 1;
            if (redirect_import_provider::Derived::read(object) != 42) return 2;
            if (object.viaFree() != 40) return 3;
            if (object.constRead() != 38) return 4;
            if (redirect_import_provider::freeAlias(object) != 44) return 5;
            if (redirect_import_provider::chain(object) != 46) return 6;
            callback : &(int):int = object.read;
            if (callback(5) != 42) return 7;
            unbound : redirect_import_provider::Base::*(int):int
                = redirect_import_provider::Derived::read;
            base : redirect_import_provider::Base;
            if ((base.*unbound)(5) != 42) return 8;
            freeCallback : &(redirect_import_provider::Base&, int):int
                = redirect_import_provider::chain;
            if (freeCallback(base, 5) != 42) return 9;
            if (localAlias(object) != 48) return 10;
            if (localChain(object) != 50) return 11;
            if (Renamed(object) != 46) return 12;
            returned : redirect_import_provider::Base::*(int):int = returnMember();
            if (callMember(returned, base, 5) != 42) return 13;
            return 42;
        }
    )K");
    INFO(result.out);
    INFO(result.err);
    CHECK(result.exit_code == 42);
}

TEST_CASE("member callable owners still reject nonaggregate types",
          "[import][redirect-contract][error][regression]") {
    const char* owner = GENERATE("Scalar", "Choice");
    INFO(owner);
    auto compiler = k::compiler::create();
    try {
        compiler->parse_source("redirect-invalid-callable-owner.k",
            std::string(R"K(
                module k;
                alias Scalar : int;
                union Choice { value : int; }
                test() {
                    callback : )K") + owner + "::*(int):int; }", true, false);
        FAIL("Expected a nonaggregate member callable owner diagnostic");
    } catch (const k::log::compiler_error& error) {
        CHECK(error.get_diagnostic().code
            == static_cast<unsigned int>(k::diag::type_diag::ERR_SIGNATURE_STRUCT_WRONG_KIND));
    }
}

TEST_CASE("consumer redirects preserve imported link parameter and result signatures",
          "[import][redirect-contract][imported-link][e2e][regression]") {
    auto result = build_exec_with_lib(R"K(
        module redirect_import_links;
        public accept(value : int+) : int { return *value; }
        public keep(value : int+) : int+ { return value; }
    )K", R"K(
        module redirect_import_links_consumer;
        import redirect_import_links;
        acceptLink(value : int+) : int -> redirect_import_links::accept;
        keepLink(value : int+) : int+ -> redirect_import_links::keep;
        main() : int {
            value : int = 41;
            mutableLink : int+ = &value;
            if (acceptLink(mutableLink) != 41) return 1;
            returned : int+ = keepLink(mutableLink);
            *returned = 42;
            return acceptLink(mutableLink);
        }
    )K");
    INFO(result.out);
    INFO(result.err);
    CHECK(result.exit_code == 42);
}

TEST_CASE("imported redirect reexports retain independent defaults and template source calls",
          "[import][redirect-contract][e2e][regression]") {
    std::vector<LibSpec> libraries = {
        {REDIRECT_LIB_SRC},
        {R"K(
            module redirect_import_reexport;
            import redirect_import_provider;
            public:
            alias Derived : redirect_import_provider::Derived;
            forwarded(object : Derived&, renamed : int = 2) : int
                -> redirect_import_provider::freeAlias;
            chain(object : Derived&, renamed : int = 4) : int
                -> redirect_import_provider::chain;
            sibling(object : Derived&, independent : int = 6) : int
                -> redirect_import_provider::chain;
            template<typename T>
            apply(object : T&) : int { return redirect_import_provider::chain(object); }
        )K"}
    };
    auto result = build_exec_with_libs(libraries, R"K(
        module redirect_import_reexport_consumer;
        import redirect_import_reexport;
        main() : int {
            object : redirect_import_reexport::Derived;
            if (redirect_import_reexport::forwarded(object) != 39) return 1;
            if (redirect_import_reexport::chain(object) != 41) return 2;
            if (redirect_import_reexport::apply(object) != 46) return 3;
            if (redirect_import_reexport::sibling(object) != 43) return 4;
            return 42;
        }
    )K");
    INFO(result.out);
    INFO(result.err);
    CHECK(result.exit_code == 42);
}

TEST_CASE("consumer redirect homonyms select imported source overloads",
          "[import][redirect-contract][e2e][regression]") {
    auto result = build_exec_with_lib(R"K(
        module redirect_import_overloads;
        public:
        target(value : long) : long { return value; }
        target(value : int = 40) : int { return value + 2; }
    )K", R"K(
        module redirect_import_overloads_consumer;
        import redirect_import_overloads;
        target(source : int = 1) : int -> redirect_import_overloads::target(int);
        other(independent : int = 2) : int -> redirect_import_overloads::target(int);
        main() : int {
            if (target() != 3) return 1;
            if (other() != 4) return 2;
            if (redirect_import_overloads::target() != 42) return 3;
            return 42;
        }
    )K");
    INFO(result.out);
    INFO(result.err);
    CHECK(result.exit_code == 42);
}

TEST_CASE("imported virtual redirect calls use the actual provider receiver",
          "[import][redirect-contract][virtual][e2e][regression]") {
    auto result = build_exec_with_lib(R"K(
        module redirect_import_virtual;
        public:
        class Base {
            value : int = 37;
            read() : int { return value; }
        }
        class Derived : public Base { read() : int -> Base::read; }
    )K", R"K(
        module redirect_import_virtual_consumer;
        import redirect_import_virtual;
        dispatch(object : redirect_import_virtual::Base&) : int { return object.read(); }
        main() : int {
            object : redirect_import_virtual::Derived;
            callback : &():int = object.read;
            if (object.read() != 37) return 1;
            if (redirect_import_virtual::Derived::read(object) != 37) return 2;
            if (dispatch(object) != 37) return 3;
            if (callback() != 37) return 4;
            unbound : redirect_import_virtual::Base::*():int
                = redirect_import_virtual::Derived::read;
            base : redirect_import_virtual::Base;
            if ((base.*unbound)() != 37) return 5;
            return 42;
        }
    )K");
    INFO(result.out);
    INFO(result.err);
    CHECK(result.exit_code == 42);
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

// ─────────────────────────────────────────────────────────────────────────────
// Same-owner covariant twins share one mangled name (return types are not
// mangled) but are two distinct provider bodies. The KDI must record the symbol
// of the body each method actually has, with a parseable bodiless prototype.
// The fixture uses concrete classes only: abstract methods and interface vtables
// carry an empty llvm_def, which kdi_validate rejects (known open contract mismatch
// tracked in TODO.md, independent of this regression).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

constexpr const char* COVARIANT_TWIN_LIB_SRC = R"K(
    module covariant_twin_provider;
    public:
    struct Big { a : long; b : long; c : long; }
    makeBig(seed : long) : Big {
        result : Big;
        result.a = seed;
        result.b = seed;
        result.c = seed;
        return result;
    }
    class Sink { write(value : int) : Sink& { return this; } }
    class Tagged : public Sink {
        write(value : int) : Tagged& { return this; }
        tag() : int { return 1; }
    }
    class Twin : public Tagged {
        plain : int = 0;
        tagged : int = 0;
        override write(value : int) : Sink& { ++plain; return this; }
        override write(value : int) : Tagged& { ++tagged; return this; }
        override tag() : int { return 7; }
        plainCount() : int { return plain; }
        taggedCount() : int { return tagged; }
    }
)K";

} // anonymous namespace

TEST_CASE("KDI exports each covariant twin body with a parseable exact prototype",
          "[import][covariant-twin][regression]") {
    auto compiler = k::compiler::create(make_pic_target_machine());
    compiler->parse_source("covariant-twin-provider.k", COVARIANT_TWIN_LIB_SRC, false, false);
    auto context = compiler->get_context_for_test();
    // Function attributes of the emitted fixture must survive the exported prototype.
    llvm::Function* attributed = nullptr;
    for (auto& candidate : context->module())
        if (!candidate.isDeclaration() && candidate.getName().contains("makeBig")) attributed = &candidate;
    REQUIRE(attributed);
    attributed->addFnAttr(llvm::Attribute::NoUnwind);
    attributed->addFnAttr(llvm::Attribute::getWithStackAlignment(
        context->llvm_context(), llvm::Align(16)));
    k::model::kdi_builder builder(*context);
    compiler->get_unit()->accept(builder);
    auto file = builder.take();
    const auto validation = kdi::kdi_validate(file);
    for (const auto& error : validation.errors)
        UNSCOPED_INFO("KDI validation error: " << error.path << ": " << error.message);
    REQUIRE(validation.is_valid());

    const auto& root = file.unit.root_ns;
    auto twin = std::find_if(root.aggregates.begin(), root.aggregates.end(),
        [](const auto& aggregate) { return aggregate.name == "Twin"; });
    REQUIRE(twin != root.aggregates.end());
    std::vector<const kdi::kdi_method*> writes;
    for (const auto& method : twin->methods)
        if (method.name == "write") writes.push_back(&method);
    REQUIRE(writes.size() == 2);

    // Distinct bodies, each declared exactly as the provider emitted it.
    CHECK(writes[0]->mangled_name != writes[1]->mangled_name);
    for (const auto* method : writes) {
        CAPTURE(method->mangled_name, method->llvm_def);
        auto* emitted = context->module().getFunction(method->mangled_name);
        REQUIRE(emitted);
        CHECK(method->llvm_def.rfind("declare ", 0) == 0);
        CHECK(method->llvm_def.find("@" + method->mangled_name + "(") != std::string::npos);
        CHECK(context->declare_llvm_function_from_def(method->llvm_def, method->mangled_name) == emitted);
    }
    REQUIRE(twin->vtable);
    for (const auto* method : writes) {
        CHECK(std::any_of(twin->vtable->slots.begin(), twin->vtable->slots.end(),
            [&](const auto& slot) { return slot.override_symbol == method->mangled_name; }));
        const auto& table = twin->vtable->llvm_def;
        CHECK((table.find("@" + method->mangled_name + ",") != std::string::npos
               || table.find("@" + method->mangled_name + " }") != std::string::npos));
    }

    // A struct result keeps its sret attribute and calling convention in the prototype.
    auto make_big = std::find_if(root.functions.begin(), root.functions.end(),
        [](const auto& function) { return function.name == "makeBig"; });
    REQUIRE(make_big != root.functions.end());
    CAPTURE(make_big->llvm_def);
    CHECK(make_big->llvm_def.rfind("declare ", 0) == 0);
    CHECK(make_big->llvm_def.find("sret(") != std::string::npos);
    CHECK(make_big->llvm_def.find("nounwind") != std::string::npos);
    CHECK(make_big->llvm_def.find("alignstack(16)") != std::string::npos);
    auto* big = context->module().getFunction(make_big->mangled_name);
    REQUIRE(big);
    CHECK(big->hasParamAttribute(0, llvm::Attribute::StructRet));
    CHECK(big->hasFnAttribute(llvm::Attribute::NoUnwind));
    CHECK(big->getFnStackAlign() == llvm::Align(16));
    // Re-declaring the exported text must reproduce the exact function type, calling
    // convention and attributes (strict comparison against the emitted function).
    CHECK(context->declare_llvm_function_from_def(make_big->llvm_def, make_big->mangled_name) == big);
}

TEST_CASE("imported covariant twins dispatch to their own body and return their own subobject",
          "[import][covariant-twin][e2e][regression]") {
    auto result = build_exec_with_lib(COVARIANT_TWIN_LIB_SRC, R"K(
        module covariant_twin_consumer;
        import covariant_twin_provider;
        class Derived : public covariant_twin_provider::Twin {}
        main() : int {
            object : Derived;
            sink : covariant_twin_provider::Sink& = object;
            tagged : covariant_twin_provider::Tagged& = object;
            sink.write(1);
            tagged.write(2);
            if (object.plainCount() != 1 || object.taggedCount() != 1) return 1;
            again : covariant_twin_provider::Tagged& = tagged.write(3);
            if (again.tag() != 7) return 2;
            if (object.plainCount() != 1 || object.taggedCount() != 2) return 3;
            plain : covariant_twin_provider::Sink& = sink.write(4);
            if (object.plainCount() != 2 || object.taggedCount() != 2) return 4;
            if (&again != &tagged || &plain != &sink) return 5;
            big : covariant_twin_provider::Big = covariant_twin_provider::makeBig(5);
            if (big.a != 5 || big.b != 5 || big.c != 5) return 6;
            return 42;
        }
    )K");
    INFO(result.out);
    INFO(result.err);
    CHECK(result.exit_code == 42);
}
